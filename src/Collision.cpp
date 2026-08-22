#include "Collision.h"

#include "Havok.h"
#include "RE/H/hkpWorld.h"
#include "ScaleMath.h"
#include "Settings.h"
#include "WallClip.h"
#include "TDM_API.h"
#include "TrueHUD_API.h"

#include <string_view>

#include <algorithm>
#include <limits>

namespace Collision
{
	namespace
	{
		constexpr std::uint32_t kMaxActors = 64;
		constexpr std::uint32_t kMaxTracked = 128;
		constexpr std::uint32_t kUpdateInterval = 8;
		constexpr std::uint32_t kCombatUpdateInterval = 1;
		constexpr float kMaxGrowPerTick = 1.08f;
		constexpr float kHumanXYRadius = 18.0f;
		constexpr float kSlideXYWorld = 8.0f;
		constexpr float kMinSlideXYWorld = 6.0f;
		constexpr float kSlideHeightWorld = 40.0f;
		constexpr float kMinSlideHeightWorld = 28.0f;
		constexpr float kSlideGroundFallTime = 0.2f;
		constexpr const char* kSkyParkourSliding = "SkyParkourSliding";
		constexpr const char* kSkyParkourIsLandingRoll = "SkyParkourIsLandingRoll";
		constexpr float kMinVanillaXY = 12.0f;
		constexpr float kMaxVanillaXY = 24.0f;
		constexpr float kMaxVcdXY = 96.0f;
		constexpr float kMaxApplied = 8.00f;

		struct HullSnapshot
		{
			std::vector<RE::hkpConvexVerticesShape::FourVectors> rotatedVertices;
			std::vector<RE::hkVector4>                           planeEquations;
			RE::hkVector4                                        aabbHalfExtents{};
			RE::hkVector4                                        aabbCenter{};
			RE::hkVector4                                        listAabbHalfExtents{};
			RE::hkVector4                                        listAabbCenter{};
			bool                                                 valid{ false };
		};

		struct Tracked
		{
			RE::ActorHandle handle;
			RE::bhkCharacterController* controller{ nullptr };
			const RE::hkpShape* shape{ nullptr };
			RE::hkpConvexVerticesShape* convex{ nullptr };
			float applied{ ScaleMath::kVanilla };
			float vanillaRadius{ 0.0f };
			HullSnapshot combatHull;
			HullSnapshot slideHull;
			bool slideActive{ false };
		};

		std::unordered_map<RE::FormID, Tracked> g_tracked;
		std::unordered_map<RE::FormID, float> g_vcdBase;
		bool g_vcdFightOverride = false;
		bool g_skyParkourPresent = false;
		bool g_slideSession = false;
		bool g_slideFromGround = false;
		bool g_postLoadSanitize = false;

		bool FindConvexList(
			RE::bhkCharacterController* a_controller,
			RE::hkpListShape*& a_list,
			RE::hkpConvexVerticesShape*& a_convex);
		float MeasureConvexXYWorld(RE::hkpConvexVerticesShape* a_convex);
		bool ActorUsable(RE::Actor* a_actor)
		{
			if (!a_actor || a_actor->IsDisabled() || a_actor->IsDeleted() || !a_actor->Is3DLoaded()) {
				return false;
			}
			if (a_actor->IsDead() || a_actor->IsInKillMove() || a_actor->IsInRagdollState()) {
				return false;
			}
			return a_actor->GetCharController() != nullptr;
		}

		bool ContainsInsensitive(std::string_view a_hay, std::string_view a_needle)
		{
			if (a_needle.empty() || a_hay.size() < a_needle.size()) {
				return false;
			}
			const auto norm = [](char a_ch) {
				return (a_ch >= 'A' && a_ch <= 'Z') ? static_cast<char>(a_ch - 'A' + 'a') : a_ch;
			};
			for (std::size_t i = 0; i + a_needle.size() <= a_hay.size(); ++i) {
				bool match = true;
				for (std::size_t j = 0; j < a_needle.size(); ++j) {
					if (norm(a_hay[i + j]) != norm(a_needle[j])) {
						match = false;
						break;
					}
				}
				if (match) {
					return true;
				}
			}
			return false;
		}

		std::string_view RaceEditorID(RE::TESRace* a_race)
		{
			if (!a_race) {
				return {};
			}
			const char* editorID = a_race->GetFormEditorID();
			if (!editorID || editorID[0] == '\0') {
				return {};
			}
			return editorID;
		}

		bool IsBigfootRace(RE::TESRace* a_race)
		{
			const auto id = RaceEditorID(a_race);
			if (id.empty() || ContainsInsensitive(id, "Spider")) {
				return false;
			}
			return ContainsInsensitive(id, "Troll") ||
				ContainsInsensitive(id, "Yeti") ||
				ContainsInsensitive(id, "Sasquatch") ||
				ContainsInsensitive(id, "Bigfoot");
		}

		bool ShouldScaleHull(RE::Actor* a_actor)
		{
			if (!a_actor) {
				return false;
			}
			if (a_actor->IsPlayerRef()) {
				return true;
			}

			auto* race = a_actor->GetRace();
			if (!race) {
				return false;
			}

			if (race->HasKeywordString("ActorTypeNPC"sv)) {
				return true;
			}
			return IsBigfootRace(race);
		}

		void ClearSlideSession()
		{
			g_slideSession = false;
			g_slideFromGround = false;
		}

		bool IsSkyParkourCrouchSlide(RE::Actor* a_actor)
		{
			if (!g_skyParkourPresent || !a_actor || !a_actor->IsPlayerRef()) {
				return false;
			}

			bool sliding = false;
			if (!a_actor->GetGraphVariableBool(kSkyParkourSliding, sliding) || !sliding) {
				ClearSlideSession();
				return false;
			}

			bool isRoll = false;
			a_actor->GetGraphVariableBool(kSkyParkourIsLandingRoll, isRoll);
			if (isRoll) {
				ClearSlideSession();
				return false;
			}

			if (!g_slideSession) {
				g_slideSession = true;
				const auto* ctrl = a_actor->GetCharController();
				const float fallTime = ctrl ? ctrl->fallTime : 0.0f;
				g_slideFromGround = !a_actor->IsInMidair() && fallTime <= kSlideGroundFallTime;
				if (g_slideFromGround) {
					logger::debug("skyparkour crouch slide start {:08X}", a_actor->GetFormID());
				}
			}
			return g_slideFromGround;
		}

		bool PlayerSlideActive(RE::Actor* a_player)
		{
			if (!a_player) {
				return false;
			}
			const auto it = g_tracked.find(a_player->GetFormID());
			return it != g_tracked.end() && it->second.slideActive;
		}

		bool PlayerNeedsSlideTrack(RE::Actor* a_player)
		{
			return IsSkyParkourCrouchSlide(a_player) || PlayerSlideActive(a_player);
		}

		bool PlayerCombatHullMode(RE::Actor* a_player)
		{
			if (!Settings::bEnabled || !a_player) {
				return false;
			}
			if (Settings::bLockTargetOnly) {
				const auto* tdm = TDM_API::GetInterface();
				return tdm && tdm->GetTargetLockState();
			}
			return a_player->IsInCombat();
		}

		bool VanillaHullScalable(float a_vanillaRadius)
		{
			return a_vanillaRadius >= kMinVanillaXY && a_vanillaRadius <= kMaxVanillaXY;
		}

		bool VcdPresetScalable(float a_radius)
		{
			return a_radius >= kMinVanillaXY && a_radius <= kMaxVcdXY;
		}

		float VcdBaseRadius(RE::FormID a_formID, float a_fallback)
		{
			const auto it = g_vcdBase.find(a_formID);
			if (it != g_vcdBase.end() && ScaleMath::IsFinitePositive(it->second)) {
				return it->second;
			}
			if (VanillaHullScalable(a_fallback)) {
				return a_fallback;
			}
			return 0.0f;
		}

		void RememberVcdBase(RE::FormID a_formID, float a_radius)
		{
			if (!ScaleMath::IsFinitePositive(a_radius)) {
				return;
			}
			if (!VanillaHullScalable(a_radius) && !VcdPresetScalable(a_radius)) {
				return;
			}
			if (g_vcdBase.size() >= kMaxTracked && !g_vcdBase.contains(a_formID)) {
				return;
			}
			g_vcdBase[a_formID] = a_radius;
		}

		bool IsActorCollisionLayer(RE::COL_LAYER a_layer)
		{
			switch (a_layer) {
			case RE::COL_LAYER::kBiped:
			case RE::COL_LAYER::kCharController:
			case RE::COL_LAYER::kDeadBip:
			case RE::COL_LAYER::kBipedNoCC:
				return true;
			default:
				return false;
			}
		}

		bool InPlayersFight(RE::Actor* a_actor, RE::Actor* a_player)
		{
			if (!a_actor || !a_player) {
				return false;
			}
			if (a_actor == a_player) {
				return a_player->IsInCombat();
			}
			if (!a_actor->IsInCombat()) {
				return false;
			}
			if (a_actor->IsPlayerTeammate()) {
				return true;
			}

			const auto combatTarget = a_player->GetActorRuntimeData().currentCombatTarget.get();
			if (combatTarget.get() == a_actor) {
				return true;
			}

			if (a_actor->GetCurrentScene()) {
				return false;
			}
			return a_actor->IsHostileToActor(a_player) || a_player->IsHostileToActor(a_actor);
		}

		bool InAllyFight(RE::Actor* a_actor, RE::Actor* a_ally)
		{
			if (!a_actor || !a_ally || a_actor == a_ally || !a_actor->IsInCombat()) {
				return false;
			}
			if (a_actor->GetCurrentScene() || a_ally->GetCurrentScene()) {
				return false;
			}

			const auto actorTarget = a_actor->GetActorRuntimeData().currentCombatTarget.get();
			if (actorTarget.get() == a_ally) {
				return true;
			}
			const auto allyTarget = a_ally->GetActorRuntimeData().currentCombatTarget.get();
			if (allyTarget.get() == a_actor) {
				return true;
			}
			return a_actor->IsHostileToActor(a_ally) || a_ally->IsHostileToActor(a_actor);
		}

		void CollectAllyFights(
			const std::function<void(RE::Actor*)>& a_add,
			const RE::ProcessLists* a_lists)
		{
			if (!a_lists) {
				return;
			}

			RE::Actor* allies[kMaxActors]{};
			std::uint32_t allyCount = 0;
			for (auto& handle : a_lists->highActorHandles) {
				const auto actor = handle.get();
				if (!actor || !actor->IsPlayerTeammate() || !actor->IsInCombat()) {
					continue;
				}
				if (allyCount >= kMaxActors) {
					break;
				}
				allies[allyCount++] = actor.get();
				a_add(actor.get());
			}
			if (allyCount == 0) {
				return;
			}

			for (auto& handle : a_lists->highActorHandles) {
				const auto actor = handle.get();
				if (!actor || !actor->IsInCombat()) {
					continue;
				}
				for (std::uint32_t i = 0; i < allyCount; ++i) {
					if (actor.get() == allies[i]) {
						continue;
					}
					if (InAllyFight(actor.get(), allies[i])) {
						a_add(actor.get());
						break;
					}
				}
			}
		}

		void CollectWanted(std::unordered_set<RE::FormID>& a_out)
		{
			a_out.clear();

			auto* player = RE::PlayerCharacter::GetSingleton();
			const auto add = [&](RE::Actor* a_actor) {
				if (!ActorUsable(a_actor) || !ShouldScaleHull(a_actor)) {
					return;
				}
				if (a_out.size() >= kMaxActors) {
					return;
				}
				a_out.insert(a_actor->GetFormID());
			};

			if (Settings::bEnabled) {
				if (Settings::bLockTargetOnly) {
					const auto* tdm = TDM_API::GetInterface();
					if (tdm && tdm->GetTargetLockState()) {
						add(player);
						if (const auto target = tdm->GetCurrentTarget().get()) {
							if (!ShouldScaleHull(target.get())) {
								static RE::FormID lastSkip = 0;
								const auto id = target->GetFormID();
								if (id != lastSkip) {
									lastSkip = id;
									logger::debug("skip non-humanoid lock target {:08X}", id);
								}
							}
							add(target.get());
						}
					}
				} else if (player && player->IsInCombat()) {
					add(player);
					const auto* lists = RE::ProcessLists::GetSingleton();
					if (lists) {
						for (auto& handle : lists->highActorHandles) {
							const auto actor = handle.get();
							if (actor && InPlayersFight(actor.get(), player)) {
								add(actor.get());
							}
						}
					}
				}

				const auto* lists = RE::ProcessLists::GetSingleton();
				if (Settings::bAllyCombatCollision && lists) {
					CollectAllyFights(add, lists);
				}
			}

			if (PlayerNeedsSlideTrack(player)) {
				add(player);
			}
		}

		bool FindConvex(
			RE::hkpWorldObject* a_body,
			RE::hkpListShape*& a_list,
			RE::hkpConvexVerticesShape*& a_convex)
		{
			a_list = nullptr;
			a_convex = nullptr;
			if (!a_body) {
				return false;
			}

			auto* shape = const_cast<RE::hkpShape*>(a_body->collidable.shape);
			if (!shape) {
				return false;
			}

			a_list = skyrim_cast<RE::hkpListShape*>(shape);
			auto* convexSrc = a_list && a_list->childInfo.size() > 0 ?
			                      const_cast<RE::hkpShape*>(a_list->childInfo[0].shape) :
			                      shape;
			a_convex = skyrim_cast<RE::hkpConvexVerticesShape*>(convexSrc);
			return a_convex != nullptr;
		}

		bool ScaleCapsules(RE::hkpShape* a_shape, float a_factor, int a_depth)
		{
			if (!a_shape || a_depth > 4) {
				return false;
			}

			if (a_shape->type == RE::hkpShapeType::kCapsule) {
				if (auto* capsule = skyrim_cast<RE::hkpCapsuleShape*>(a_shape)) {
					capsule->radius *= a_factor;
					return true;
				}
				return false;
			}

			if (a_shape->type != RE::hkpShapeType::kList) {
				return false;
			}

			auto* list = skyrim_cast<RE::hkpListShape*>(a_shape);
			if (!list) {
				return false;
			}

			bool any = false;
			const auto count = list->childInfo.size();
			for (RE::hkArray<RE::hkpListShape::ChildInfo>::size_type i = 0; i < count; ++i) {
				if (auto* child = const_cast<RE::hkpShape*>(list->childInfo[i].shape)) {
					any = ScaleCapsules(child, a_factor, a_depth + 1) || any;
				}
			}
			return any;
		}

		void ScaleHkXY(RE::hkVector4& a_vec, float a_factor)
		{
			auto* q = a_vec.quad.m128_f32;
			ScaleMath::ScaleXY(q[0], q[1], a_factor);
		}

		bool ScaleConvexXYInPlace(RE::hkpListShape* a_list, RE::hkpConvexVerticesShape* a_convex, float a_factor)
		{
			if (!a_convex || a_convex->numVertices <= 0 || a_convex->rotatedVertices.empty()) {
				return false;
			}

			ScaleHkXY(a_convex->aabbHalfExtents, a_factor);

			const auto rings = a_convex->rotatedVertices.size();
			for (RE::hkArray<RE::hkpConvexVerticesShape::FourVectors>::size_type i = 0; i < rings; ++i) {
				auto& ring = a_convex->rotatedVertices[i];
				for (int component = 0; component < 4; ++component) {
					ring.x.quad.m128_f32[component] *= a_factor;
					ring.y.quad.m128_f32[component] *= a_factor;
				}
			}

			const auto planes = a_convex->planeEquations.size();
			const float inv = 1.0f / a_factor;
			for (RE::hkArray<RE::hkVector4>::size_type i = 0; i < planes; ++i) {
				auto* q = a_convex->planeEquations[i].quad.m128_f32;
				q[0] *= inv;
				q[1] *= inv;
			}

			if (a_list) {
				ScaleHkXY(a_list->aabbHalfExtents, a_factor);
			}
			return true;
		}

		bool MeasureConvexZHk(RE::hkpConvexVerticesShape* a_convex, float& a_minZ, float& a_maxZ)
		{
			a_minZ = 0.0f;
			a_maxZ = 0.0f;
			if (!a_convex || a_convex->numVertices <= 0 || a_convex->rotatedVertices.empty()) {
				return false;
			}

			const auto target = static_cast<std::size_t>(a_convex->numVertices);
			float minZ = std::numeric_limits<float>::infinity();
			float maxZ = -std::numeric_limits<float>::infinity();
			std::size_t n = 0;
			const auto rings = a_convex->rotatedVertices.size();
			for (RE::hkArray<RE::hkpConvexVerticesShape::FourVectors>::size_type i = 0; i < rings; ++i) {
				const auto& ring = a_convex->rotatedVertices[i];
				for (int component = 0; component < 4; ++component) {
					if (n >= target) {
						break;
					}
					const float z = ring.z.quad.m128_f32[component];
					minZ = std::min(minZ, z);
					maxZ = std::max(maxZ, z);
					++n;
				}
				if (n >= target) {
					break;
				}
			}
			if (n == 0 || !std::isfinite(minZ) || !std::isfinite(maxZ) || maxZ <= minZ) {
				return false;
			}
			a_minZ = minZ;
			a_maxZ = maxZ;
			return true;
		}

		float MeasureConvexHeightWorld(RE::hkpConvexVerticesShape* a_convex)
		{
			float minZ = 0.0f;
			float maxZ = 0.0f;
			if (!MeasureConvexZHk(a_convex, minZ, maxZ)) {
				return 0.0f;
			}
			return (maxZ - minZ) * RE::bhkWorld::GetWorldScaleInverse();
		}

		bool ScaleConvexSlideInPlace(
			RE::hkpListShape* a_list,
			RE::hkpConvexVerticesShape* a_convex,
			float a_sx,
			float a_sz,
			float a_minZ)
		{
			if (!a_convex || a_convex->numVertices <= 0 || a_convex->rotatedVertices.empty()) {
				return false;
			}
			if (!std::isfinite(a_sx) || a_sx <= 0.0f || !std::isfinite(a_sz) || a_sz <= 0.0f) {
				return false;
			}
			if (!std::isfinite(a_minZ)) {
				return false;
			}

			const float tz = ScaleMath::SlideZTranslate(a_minZ, a_sz);
			const auto target = static_cast<std::size_t>(a_convex->numVertices);
			float minX = std::numeric_limits<float>::infinity();
			float maxX = -std::numeric_limits<float>::infinity();
			float minY = std::numeric_limits<float>::infinity();
			float maxY = -std::numeric_limits<float>::infinity();
			float minZ = std::numeric_limits<float>::infinity();
			float maxZ = -std::numeric_limits<float>::infinity();
			std::size_t n = 0;
			const auto rings = a_convex->rotatedVertices.size();
			for (RE::hkArray<RE::hkpConvexVerticesShape::FourVectors>::size_type i = 0; i < rings; ++i) {
				auto& ring = a_convex->rotatedVertices[i];
				for (int component = 0; component < 4; ++component) {
					if (n >= target) {
						break;
					}
					float x = ring.x.quad.m128_f32[component] * a_sx;
					float y = ring.y.quad.m128_f32[component] * a_sx;
					float z = ScaleMath::CompressZFromMin(
						ring.z.quad.m128_f32[component], a_minZ, a_sz);
					ring.x.quad.m128_f32[component] = x;
					ring.y.quad.m128_f32[component] = y;
					ring.z.quad.m128_f32[component] = z;
					minX = std::min(minX, x);
					maxX = std::max(maxX, x);
					minY = std::min(minY, y);
					maxY = std::max(maxY, y);
					minZ = std::min(minZ, z);
					maxZ = std::max(maxZ, z);
					++n;
				}
				if (n >= target) {
					break;
				}
			}
			if (n == 0 ||
				!std::isfinite(minX) || !std::isfinite(maxX) ||
				!std::isfinite(minY) || !std::isfinite(maxY) ||
				!std::isfinite(minZ) || !std::isfinite(maxZ)) {
				return false;
			}

			const auto planes = a_convex->planeEquations.size();
			for (RE::hkArray<RE::hkVector4>::size_type i = 0; i < planes; ++i) {
				auto* q = a_convex->planeEquations[i].quad.m128_f32;
				ScaleMath::ScaleSlidePlane(q[0], q[1], q[2], q[3], a_sx, a_sx, a_sz, tz);
			}

			auto* half = a_convex->aabbHalfExtents.quad.m128_f32;
			auto* center = a_convex->aabbCenter.quad.m128_f32;
			center[0] = (minX + maxX) * 0.5f;
			center[1] = (minY + maxY) * 0.5f;
			center[2] = (minZ + maxZ) * 0.5f;
			half[0] = (maxX - minX) * 0.5f;
			half[1] = (maxY - minY) * 0.5f;
			half[2] = (maxZ - minZ) * 0.5f;
			if (a_list) {
				a_list->aabbHalfExtents = a_convex->aabbHalfExtents;
				a_list->aabbCenter = a_convex->aabbCenter;
			}
			return true;
		}

		void CopyHkArrayVertices(
			std::vector<RE::hkpConvexVerticesShape::FourVectors>& a_out,
			const RE::hkArray<RE::hkpConvexVerticesShape::FourVectors>& a_in)
		{
			a_out.resize(static_cast<std::size_t>(a_in.size()));
			for (RE::hkArray<RE::hkpConvexVerticesShape::FourVectors>::size_type i = 0; i < a_in.size(); ++i) {
				a_out[static_cast<std::size_t>(i)] = a_in[i];
			}
		}

		void CopyHkArrayPlanes(
			std::vector<RE::hkVector4>& a_out,
			const RE::hkArray<RE::hkVector4>& a_in)
		{
			a_out.resize(static_cast<std::size_t>(a_in.size()));
			for (RE::hkArray<RE::hkVector4>::size_type i = 0; i < a_in.size(); ++i) {
				a_out[static_cast<std::size_t>(i)] = a_in[i];
			}
		}

		void WriteHkArrayVertices(
			RE::hkArray<RE::hkpConvexVerticesShape::FourVectors>& a_out,
			const std::vector<RE::hkpConvexVerticesShape::FourVectors>& a_in)
		{
			a_out.resize(static_cast<RE::hkArray<RE::hkpConvexVerticesShape::FourVectors>::size_type>(a_in.size()));
			for (std::size_t i = 0; i < a_in.size(); ++i) {
				a_out[static_cast<RE::hkArray<RE::hkpConvexVerticesShape::FourVectors>::size_type>(i)] =
					a_in[i];
			}
		}

		void WriteHkArrayPlanes(
			RE::hkArray<RE::hkVector4>& a_out,
			const std::vector<RE::hkVector4>& a_in)
		{
			a_out.resize(static_cast<RE::hkArray<RE::hkVector4>::size_type>(a_in.size()));
			for (std::size_t i = 0; i < a_in.size(); ++i) {
				a_out[static_cast<RE::hkArray<RE::hkVector4>::size_type>(i)] = a_in[i];
			}
		}

		void SnapshotCombatHull(
			HullSnapshot& a_snap,
			RE::hkpListShape* a_list,
			RE::hkpConvexVerticesShape* a_convex)
		{
			a_snap.rotatedVertices.clear();
			a_snap.planeEquations.clear();
			if (!a_convex || a_convex->numVertices <= 0 || a_convex->rotatedVertices.empty()) {
				a_snap.valid = false;
				return;
			}

			CopyHkArrayVertices(a_snap.rotatedVertices, a_convex->rotatedVertices);
			CopyHkArrayPlanes(a_snap.planeEquations, a_convex->planeEquations);
			a_snap.aabbHalfExtents = a_convex->aabbHalfExtents;
			a_snap.aabbCenter = a_convex->aabbCenter;
			a_snap.listAabbHalfExtents = a_list ? a_list->aabbHalfExtents : RE::hkVector4{};
			a_snap.listAabbCenter = a_list ? a_list->aabbCenter : RE::hkVector4{};
			a_snap.valid = true;
		}

		void RestoreCombatHull(
			const HullSnapshot& a_snap,
			RE::hkpListShape* a_list,
			RE::hkpConvexVerticesShape* a_convex)
		{
			if (!a_snap.valid || !a_convex) {
				return;
			}
			WriteHkArrayVertices(a_convex->rotatedVertices, a_snap.rotatedVertices);
			WriteHkArrayPlanes(a_convex->planeEquations, a_snap.planeEquations);
			a_convex->aabbHalfExtents = a_snap.aabbHalfExtents;
			a_convex->aabbCenter = a_snap.aabbCenter;
			if (a_list) {
				a_list->aabbHalfExtents = a_snap.listAabbHalfExtents;
				a_list->aabbCenter = a_snap.listAabbCenter;
			}
		}

		void InvalidateCombatHull(Tracked& a_tracked)
		{
			a_tracked.combatHull.valid = false;
		}

		const RE::hkpCollidable* ManifoldOtherCollidable(
			const RE::hkpRootCdPoint& a_pt,
			const RE::hkpCollidable* a_selfCol)
		{
			if (a_pt.rootCollidableA == a_selfCol) {
				return a_pt.rootCollidableB;
			}
			if (a_pt.rootCollidableB == a_selfCol) {
				return a_pt.rootCollidableA;
			}
			return a_pt.rootCollidableB ? a_pt.rootCollidableB : a_pt.rootCollidableA;
		}

		void MaybeSnapshotCombatHull(
			Tracked& a_tracked,
			RE::hkpListShape* a_list,
			RE::hkpConvexVerticesShape* a_convex,
			float a_slider,
			float a_liveRadius,
			float a_wantCombatRadius)
		{
			if (a_tracked.combatHull.valid || !a_convex || !ScaleMath::NeedsApply(a_slider, ScaleMath::kVanilla)) {
				return;
			}
			if (ScaleMath::RadiusNeedsScale(a_liveRadius, a_wantCombatRadius)) {
				return;
			}
			SnapshotCombatHull(a_tracked.combatHull, a_list, a_convex);
		}

		bool FindConvexList(
			RE::bhkCharacterController* a_controller,
			RE::hkpListShape*& a_list,
			RE::hkpConvexVerticesShape*& a_convex)
		{
			a_list = nullptr;
			a_convex = nullptr;
			if (!a_controller) {
				return false;
			}
			auto* body = a_controller->GetBodyImpl();
			if (!body) {
				return false;
			}
			return FindConvex(body, a_list, a_convex);
		}

		float CombatAppliedFromSlider(float a_slider, float a_baseR)
		{
			return a_slider <= ScaleMath::kVanilla + ScaleMath::kEpsilon ?
				ScaleMath::kVanilla :
				(kHumanXYRadius * a_slider) / (VanillaHullScalable(a_baseR) ? a_baseR : kHumanXYRadius);
		}

		float MeasureConvexXYWorld(RE::hkpConvexVerticesShape* a_convex)
		{
			if (!a_convex) {
				return 0.0f;
			}

			RE::hkArray<RE::hkVector4> verts{};
			hkpConvexVerticesShape_getOriginalVertices(a_convex, verts);
			if (verts.size() <= 0) {
				return 0.0f;
			}

			const float inv = RE::bhkWorld::GetWorldScaleInverse();
			float maxR = 0.0f;
			for (RE::hkArray<RE::hkVector4>::size_type i = 0; i < verts.size(); ++i) {
				const float x = verts[i].quad.m128_f32[0];
				const float y = verts[i].quad.m128_f32[1];
				const float r = std::sqrt(x * x + y * y) * inv;
				if (r > maxR) {
					maxR = r;
				}
			}
			return maxR;
		}

		bool PeekLiveShape(
			RE::Actor* a_actor,
			RE::bhkCharacterController* a_controller,
			const RE::hkpShape*& a_root,
			RE::hkpConvexVerticesShape*& a_convex,
			float& a_xyRadius)
		{
			a_root = nullptr;
			a_convex = nullptr;
			a_xyRadius = 0.0f;
			if (!a_actor || !a_controller) {
				return false;
			}

			auto* cell = a_actor->GetParentCell();
			if (!cell) {
				return false;
			}
			auto* world = cell->GetbhkWorld();
			if (!world) {
				return false;
			}

			auto* body = a_controller->GetBodyImpl();
			if (!body) {
				return false;
			}

			RE::BSReadLockGuard lock(world->worldLock);
			a_root = body->collidable.shape;
			RE::hkpListShape* list = nullptr;
			FindConvex(body, list, a_convex);
			a_xyRadius = MeasureConvexXYWorld(a_convex);
			return a_root != nullptr;
		}

		void TryCacheVcdBase(RE::Actor* a_actor)
		{
			if (!g_vcdFightOverride || !ActorUsable(a_actor) || !ShouldScaleHull(a_actor)) {
				return;
			}
			if (a_actor->IsInCombat()) {
				return;
			}

			auto* controller = a_actor->GetCharController();
			const RE::hkpShape* root = nullptr;
			RE::hkpConvexVerticesShape* convex = nullptr;
			float xyRadius = 0.0f;
			if (!PeekLiveShape(a_actor, controller, root, convex, xyRadius)) {
				return;
			}
			RememberVcdBase(a_actor->GetFormID(), xyRadius);
		}

		bool ScaleControllerXY(RE::Actor* a_actor, RE::bhkCharacterController* a_controller, float a_factor)
		{
			if (!a_actor || !a_controller || !std::isfinite(a_factor) || a_factor <= 0.0f) {
				return false;
			}

			auto* cell = a_actor->GetParentCell();
			if (!cell) {
				return false;
			}
			auto* world = cell->GetbhkWorld();
			if (!world) {
				return false;
			}

			auto* body = a_controller->GetBodyImpl();
			if (!body) {
				return false;
			}

			RE::BSWriteLockGuard lock(world->worldLock);

			RE::hkpListShape* list = nullptr;
			RE::hkpConvexVerticesShape* convex = nullptr;
			if (FindConvex(body, list, convex)) {
				return ScaleConvexXYInPlace(list, convex, a_factor);
			}

			auto* shape = const_cast<RE::hkpShape*>(body->collidable.shape);
			return ScaleCapsules(shape, a_factor, 0);
		}

		void ClearSlideHull(Tracked& a_tracked)
		{
			a_tracked.slideActive = false;
			a_tracked.slideHull.valid = false;
		}

		bool RestoreSlideHull(
			RE::Actor* a_actor,
			RE::bhkCharacterController* a_controller,
			Tracked& a_tracked)
		{
			if (!a_tracked.slideHull.valid) {
				ClearSlideHull(a_tracked);
				return false;
			}

			auto* cell = a_actor ? a_actor->GetParentCell() : nullptr;
			auto* world = cell ? cell->GetbhkWorld() : nullptr;
			RE::hkpListShape* list = nullptr;
			RE::hkpConvexVerticesShape* convex = nullptr;
			FindConvexList(a_controller, list, convex);
			if (world && convex) {
				RE::BSWriteLockGuard lock(world->worldLock);
				RestoreCombatHull(a_tracked.slideHull, list, convex);
			}
			ClearSlideHull(a_tracked);
			logger::debug(
				"skyparkour crouch slide end {:08X}",
				a_actor ? a_actor->GetFormID() : 0);
			return true;
		}

		bool ApplySlideShrink(
			RE::Actor* a_actor,
			RE::bhkCharacterController* a_controller,
			Tracked& a_tracked)
		{
			if (!a_actor || !a_controller) {
				return false;
			}

			auto* cell = a_actor->GetParentCell();
			auto* world = cell ? cell->GetbhkWorld() : nullptr;
			if (!world) {
				return false;
			}

			RE::hkpListShape* list = nullptr;
			RE::hkpConvexVerticesShape* convex = nullptr;
			FindConvexList(a_controller, list, convex);
			if (!convex) {
				return false;
			}

			const float liveXY = MeasureConvexXYWorld(convex);
			const float liveH = MeasureConvexHeightWorld(convex);
			const float wantXY = std::max(kMinSlideXYWorld, kSlideXYWorld);
			const float wantH = std::max(kMinSlideHeightWorld, kSlideHeightWorld);
			const float sx = ScaleMath::ShrinkFactor(liveXY, wantXY);
			const float sz = ScaleMath::ShrinkFactor(liveH, wantH);
			const bool needXY = ScaleMath::NeedsApply(sx, 1.0f);
			const bool needZ = ScaleMath::NeedsApply(sz, 1.0f);
			if (a_tracked.slideActive && !needXY && !needZ) {
				return true;
			}

			float minZ = 0.0f;
			float maxZ = 0.0f;
			if ((needXY || needZ) && !MeasureConvexZHk(convex, minZ, maxZ)) {
				return false;
			}

			{
				RE::BSWriteLockGuard lock(world->worldLock);
				if (!a_tracked.slideHull.valid) {
					SnapshotCombatHull(a_tracked.slideHull, list, convex);
				}
				if ((needXY || needZ) &&
					!ScaleConvexSlideInPlace(list, convex, sx, sz, minZ)) {
					return false;
				}
			}

			if (!a_tracked.slideActive) {
				a_tracked.slideActive = true;
				logger::debug(
					"skyparkour crouch slide hull {:08X} xy={:.1f}->{:.1f} h={:.1f}->{:.1f}",
					a_actor->GetFormID(),
					liveXY,
					liveXY * sx,
					liveH,
					liveH * sz);
			}
			return true;
		}

		enum class WeaponPreset
		{
			Default,
			Fist,
			Dagger,
			Sword,
			Longsword
		};

		const char* PresetName(WeaponPreset a_preset)
		{
			switch (a_preset) {
			case WeaponPreset::Fist:
				return "fist";
			case WeaponPreset::Dagger:
				return "dagger";
			case WeaponPreset::Sword:
				return "sword";
			case WeaponPreset::Longsword:
				return "longsword";
			default:
				return "default";
			}
		}

		float ScaleOf(WeaponPreset a_preset)
		{
			switch (a_preset) {
			case WeaponPreset::Fist:
				return Settings::fFist;
			case WeaponPreset::Dagger:
				return Settings::fDagger;
			case WeaponPreset::Sword:
				return Settings::fSword;
			case WeaponPreset::Longsword:
				return Settings::fLongsword;
			default:
				return Settings::fCombatScale;
			}
		}

		bool HasWeapKeyword(RE::TESObjectWEAP* a_weap, const char* a_editorID)
		{
			if (!a_weap || !a_editorID) {
				return false;
			}
			auto* keyword = RE::TESForm::LookupByEditorID<RE::BGSKeyword>(a_editorID);
			return keyword && a_weap->HasKeyword(keyword);
		}

		WeaponPreset ClassifyWeapon(RE::TESForm* a_form)
		{
			if (!a_form) {
				return WeaponPreset::Fist;
			}

			auto* weap = a_form->As<RE::TESObjectWEAP>();
			if (!weap) {
				return WeaponPreset::Default;
			}

			if (HasWeapKeyword(weap, "WeapTypeDagger") || weap->IsOneHandedDagger()) {
				return WeaponPreset::Dagger;
			}
			if (HasWeapKeyword(weap, "WeapTypeGreatsword") || weap->IsTwoHandedSword()) {
				return WeaponPreset::Longsword;
			}
			if (HasWeapKeyword(weap, "WeapTypeSword") || weap->IsOneHandedSword()) {
				return WeaponPreset::Sword;
			}
			return WeaponPreset::Default;
		}

		float ScaleForPlayerWeapon(RE::Actor* a_actor);

		float ScaleForActor(RE::Actor* a_actor, RE::Actor* a_player, float a_combatScale)
		{
			if (!a_actor) {
				return ScaleMath::kVanilla;
			}
			if (!a_player || a_actor == a_player || a_actor->IsPlayerRef()) {
				return a_combatScale;
			}

			if (Settings::bAllyCombatCollision && a_actor->IsPlayerTeammate() && a_actor->IsInCombat()) {
				return ScaleForPlayerWeapon(a_actor);
			}

			if (Settings::bAllyCombatCollision && a_actor->IsInCombat()) {
				const auto allyTarget = a_actor->GetActorRuntimeData().currentCombatTarget.get();
				if (auto* ally = allyTarget.get()) {
					if (ally != a_player && ally->IsPlayerTeammate()) {
						return ScaleForPlayerWeapon(ally);
					}
				}
			}

			const bool hostile =
				a_actor->IsHostileToActor(a_player) || a_player->IsHostileToActor(a_actor);
			return ScaleMath::ScaleForRelation(false, hostile, a_combatScale);
		}

		float ScaleForPlayerWeapon(RE::Actor* a_player)
		{
			if (!a_player) {
				return Settings::fCombatScale;
			}

			auto* right = a_player->GetEquippedObject(false);
			const auto preset = ClassifyWeapon(right);
			const auto formID = right ? right->GetFormID() : 0;
			const float scale = ScaleOf(preset);

			static RE::FormID lastWeap = 0;
			static WeaponPreset lastPreset = WeaponPreset::Default;
			if (formID != lastWeap || preset != lastPreset) {
				lastWeap = formID;
				lastPreset = preset;
				logger::debug(
					"player weapon {:08X} preset={} scale={:.2f}",
					formID,
					PresetName(preset),
					scale);
			}
			return scale;
		}

		float TargetApplied(const Tracked& a_tracked, float a_slider)
		{
			if (a_slider <= ScaleMath::kVanilla + ScaleMath::kEpsilon) {
				return ScaleMath::kVanilla;
			}
			if (!VanillaHullScalable(a_tracked.vanillaRadius)) {
				return ScaleMath::kVanilla;
			}

			float applied = (kHumanXYRadius * a_slider) / a_tracked.vanillaRadius;
			if (!std::isfinite(applied)) {
				return a_slider;
			}
			if (applied < ScaleMath::kEpsilon) {
				applied = ScaleMath::kEpsilon;
			}
			if (applied > kMaxApplied) {
				applied = kMaxApplied;
			}
			return applied;
		}

		bool ApplyVcdFightScale(
			RE::Actor* a_actor,
			float a_target,
			RE::bhkCharacterController* a_controller,
			float a_liveRadius,
			Tracked& a_tracked,
			bool a_snapGrow)
		{
			const auto formID = a_actor->GetFormID();
			const float baseR = VcdBaseRadius(formID, a_tracked.vanillaRadius);
			const bool skipUniformScale = ScaleMath::NeedsApply(a_target, ScaleMath::kVanilla) &&
				!VanillaHullScalable(baseR) && !VcdPresetScalable(a_liveRadius);
			if (skipUniformScale) {
				static RE::FormID lastSizeSkip = 0;
				if (formID != lastSizeSkip) {
					lastSizeSkip = formID;
					logger::debug(
						"skip uniform scale {:08X} vanillaR={:.1f} liveR={:.1f}",
						formID,
						baseR,
						a_liveRadius);
				}
			}

			RE::hkpListShape* list = nullptr;
			RE::hkpConvexVerticesShape* convexLive = nullptr;
			FindConvexList(a_controller, list, convexLive);

			float slider = a_target;
			const bool snapGrowNow = a_snapGrow;
			float combatApplied = CombatAppliedFromSlider(a_target, baseR);

			if (ScaleMath::NeedsApply(a_target, ScaleMath::kVanilla)) {
				MaybeSnapshotCombatHull(
					a_tracked, list, convexLive, a_target, a_liveRadius,
					ScaleMath::FightOverrideWantedRadius(
						a_liveRadius, baseR, a_target, kHumanXYRadius));
			}

			if (skipUniformScale) {
				a_tracked.applied = combatApplied;
				return true;
			}

			const float wantCombatR = ScaleMath::FightOverrideWantedRadius(
				a_liveRadius, baseR, slider, kHumanXYRadius);
			combatApplied = CombatAppliedFromSlider(slider, baseR);

			if (!ScaleMath::RadiusNeedsScale(a_liveRadius, wantCombatR)) {
				MaybeSnapshotCombatHull(
					a_tracked, list, convexLive, a_target, a_liveRadius, wantCombatR);
			}

			const float wantR = wantCombatR;
			if (!ScaleMath::RadiusNeedsScale(a_liveRadius, wantR)) {
				a_tracked.applied = combatApplied;
				MaybeSnapshotCombatHull(
					a_tracked, list, convexLive, a_target, a_liveRadius, wantCombatR);
				return true;
			}

			float factor = ScaleMath::RadiusScaleFactor(a_liveRadius, wantR);
			if (wantR > a_liveRadius && a_actor->IsPlayerRef() && !snapGrowNow) {
				factor = ScaleMath::ClampGrowFactor(factor, kMaxGrowPerTick);
			}
			if (factor == 0.0f) {
				return false;
			}
			if (!ScaleControllerXY(a_actor, a_controller, factor)) {
				logger::warn("scale failed on {:08X} factor={:.3f}", formID, factor);
				return false;
			}

			const float previous = a_tracked.applied;
			a_tracked.applied = combatApplied;
			MaybeSnapshotCombatHull(
				a_tracked, list, convexLive, a_target, a_liveRadius, wantCombatR);
			if (previous <= ScaleMath::kVanilla + ScaleMath::kEpsilon ||
				slider <= ScaleMath::kVanilla + ScaleMath::kEpsilon ||
				snapGrowNow) {
				logger::debug(
					"collision xy {:08X} live={:.1f} want={:.1f} base={:.1f}",
					formID,
					a_liveRadius,
					wantR,
					baseR);
			}
			return true;
		}

		bool SetActorScale(RE::Actor* a_actor, float a_target)
		{
			if (!a_actor) {
				return false;
			}

			auto* controller = a_actor->GetCharController();
			if (!controller) {
				return false;
			}

			const RE::hkpShape* root = nullptr;
			RE::hkpConvexVerticesShape* convex = nullptr;
			float xyRadius = 0.0f;
			PeekLiveShape(a_actor, controller, root, convex, xyRadius);

			const auto formID = a_actor->GetFormID();
			auto& tracked = g_tracked[formID];
			tracked.handle = a_actor->GetHandle();

			bool snapGrow = false;
			if (g_postLoadSanitize && a_actor->IsPlayerRef()) {
				InvalidateCombatHull(tracked);
				ClearSlideHull(tracked);
				tracked.applied = ScaleMath::kVanilla;
				snapGrow = ScaleMath::NeedsApply(a_target, ScaleMath::kVanilla);
				g_postLoadSanitize = false;
				logger::debug(
					"collision sanitize player after load {:08X} liveR={:.1f} snap={}",
					formID,
					xyRadius,
					snapGrow);
			}

			const bool hadHull = tracked.controller != nullptr || tracked.shape != nullptr;
			if (tracked.controller != controller || tracked.shape != root || tracked.convex != convex) {
				const bool wasShrunk = tracked.slideActive;
				InvalidateCombatHull(tracked);
				ClearSlideHull(tracked);
				snapGrow = hadHull && ScaleMath::NeedsApply(a_target, ScaleMath::kVanilla);
				if (snapGrow) {
					if (wasShrunk) {
						logger::debug(
							"hull rebuilt {:08X} while slide active, reapplying combat xy",
							formID);
					} else {
						logger::debug("hull rebuilt {:08X} (VCD or controller) reapplying combat xy", formID);
					}
				} else if (wasShrunk) {
					logger::debug("hull rebuilt {:08X} while slide active, will reapply", formID);
				}
				tracked.controller = controller;
				tracked.shape = root;
				tracked.convex = convex;
				tracked.applied = ScaleMath::kVanilla;
				if (g_vcdFightOverride) {
					if (auto it = g_vcdBase.find(formID); it != g_vcdBase.end()) {
						tracked.vanillaRadius = it->second;
					} else if (!a_actor->IsInCombat() &&
						(VanillaHullScalable(xyRadius) || VcdPresetScalable(xyRadius))) {
						RememberVcdBase(formID, xyRadius);
						tracked.vanillaRadius = xyRadius;
					} else if (!ScaleMath::IsFinitePositive(tracked.vanillaRadius)) {
						tracked.vanillaRadius = xyRadius;
					}
				} else {
					tracked.vanillaRadius = xyRadius;
				}
			} else if (tracked.vanillaRadius <= 1.0f &&
				!ScaleMath::NeedsApply(tracked.applied, ScaleMath::kVanilla) &&
				xyRadius > 1.0f) {
				tracked.vanillaRadius = xyRadius;
			} else if (g_vcdFightOverride && !g_vcdBase.contains(formID) && !a_actor->IsInCombat() &&
				(VanillaHullScalable(xyRadius) || VcdPresetScalable(xyRadius))) {
				RememberVcdBase(formID, xyRadius);
				tracked.vanillaRadius = xyRadius;
			}

			if (g_skyParkourPresent && a_actor->IsPlayerRef()) {
				if (IsSkyParkourCrouchSlide(a_actor)) {
					return ApplySlideShrink(a_actor, controller, tracked);
				}
				if (tracked.slideActive) {
					RestoreSlideHull(a_actor, controller, tracked);
					if (!PlayerCombatHullMode(a_actor)) {
						return true;
					}
				}
			}

			if (g_vcdFightOverride) {
				return ApplyVcdFightScale(a_actor, a_target, controller, xyRadius, tracked, snapGrow);
			}

			float slider = a_target;
			RE::hkpListShape* list = nullptr;
			RE::hkpConvexVerticesShape* convexLive = nullptr;
			FindConvexList(controller, list, convexLive);
			if (!convexLive) {
				convexLive = convex;
			}

			const float combatTargetApplied = TargetApplied(tracked, a_target);
			const float wantCombatRPre =
				ScaleMath::FightOverrideWantedRadius(
					xyRadius, tracked.vanillaRadius, a_target, kHumanXYRadius);

			if (ScaleMath::NeedsApply(a_target, ScaleMath::kVanilla)) {
				MaybeSnapshotCombatHull(
					tracked, list, convexLive, a_target, xyRadius, wantCombatRPre);
			}

			const float wantCombatR = ScaleMath::FightOverrideWantedRadius(
				xyRadius, tracked.vanillaRadius, slider, kHumanXYRadius);

			if (!ScaleMath::NeedsApply(tracked.applied, combatTargetApplied)) {
				MaybeSnapshotCombatHull(
					tracked, list, convexLive, slider, xyRadius, wantCombatR);
			}

			const float targetApplied = combatTargetApplied;
			if (ScaleMath::NeedsApply(a_target, ScaleMath::kVanilla) &&
				!VanillaHullScalable(tracked.vanillaRadius)) {
				static RE::FormID lastSizeSkip = 0;
				if (formID != lastSizeSkip) {
					lastSizeSkip = formID;
					logger::debug("skip hull size {:08X} vanillaR={:.1f}", formID, tracked.vanillaRadius);
				}
			}
			if (!ScaleMath::NeedsApply(tracked.applied, targetApplied)) {
				MaybeSnapshotCombatHull(
					tracked, list, convexLive, slider, xyRadius, wantCombatR);
				return true;
			}

			if (!ScaleMath::IsFinitePositive(tracked.applied) || !std::isfinite(targetApplied) ||
				targetApplied < ScaleMath::kEpsilon) {
				return false;
			}

			float factor = targetApplied / tracked.applied;
			if (targetApplied > tracked.applied && a_actor->IsPlayerRef() && !snapGrow) {
				factor = ScaleMath::ClampGrowFactor(factor, kMaxGrowPerTick);
			}
			if (factor == 0.0f) {
				return false;
			}
			if (!ScaleControllerXY(a_actor, controller, factor)) {
				logger::warn("scale failed on {:08X} factor={:.3f}", formID, factor);
				return false;
			}

			const float previous = tracked.applied;
			tracked.applied *= factor;
			float liveAfter = xyRadius;
			if (convexLive) {
				liveAfter = MeasureConvexXYWorld(convexLive);
			}
			MaybeSnapshotCombatHull(
				tracked, list, convexLive, slider, liveAfter, wantCombatR);
			if (previous <= ScaleMath::kVanilla + ScaleMath::kEpsilon ||
				targetApplied <= ScaleMath::kVanilla + ScaleMath::kEpsilon) {
				logger::debug(
					"collision xy {:08X} {:.2f} -> {:.2f} vanillaR={:.1f}",
					formID,
					previous,
					tracked.applied,
					tracked.vanillaRadius);
			}
			return true;
		}

		void RestoreForm(RE::FormID a_formID)
		{
			const auto it = g_tracked.find(a_formID);
			if (it == g_tracked.end()) {
				return;
			}

			const auto actor = it->second.handle.get();
			if (!actor || actor->IsDeleted()) {
				g_tracked.erase(a_formID);
				return;
			}

			if (!actor->Is3DLoaded() || !actor->GetCharController()) {
				logger::debug(
					"collision restore skip {:08X} (actor not ready)",
					a_formID);
				if (g_tracked.size() > kMaxTracked) {
					g_tracked.erase(a_formID);
				}
				return;
			}

			auto* controller = actor->GetCharController();
			RE::hkpListShape* list = nullptr;
			RE::hkpConvexVerticesShape* convex = nullptr;
			FindConvexList(controller, list, convex);
			auto* cell = actor->GetParentCell();
			auto* world = cell ? cell->GetbhkWorld() : nullptr;
			if (it->second.slideHull.valid && world && convex) {
				RE::BSWriteLockGuard lock(world->worldLock);
				RestoreCombatHull(it->second.slideHull, list, convex);
				logger::debug("collision restore slide {:08X}", a_formID);
			}
			if (it->second.combatHull.valid && world && convex) {
				RE::BSWriteLockGuard lock(world->worldLock);
				RestoreCombatHull(it->second.combatHull, list, convex);
				logger::debug("collision restore snapshot {:08X}", a_formID);
			}
			InvalidateCombatHull(it->second);
			ClearSlideHull(it->second);

			if (!ScaleMath::NeedsApply(it->second.applied, ScaleMath::kVanilla)) {
				g_tracked.erase(a_formID);
				return;
			}

			if (g_vcdFightOverride) {
				const RE::hkpShape* root = nullptr;
				RE::hkpConvexVerticesShape* convex = nullptr;
				float liveR = 0.0f;
				if (!PeekLiveShape(actor.get(), controller, root, convex, liveR)) {
					if (g_tracked.size() > kMaxTracked) {
						g_tracked.erase(a_formID);
					}
					return;
				}
				const float baseR = VcdBaseRadius(a_formID, it->second.vanillaRadius);
				const float wantR = ScaleMath::FightOverrideWantedRadius(
					liveR, baseR, ScaleMath::kVanilla, kHumanXYRadius);
				if (!ScaleMath::RadiusNeedsScale(liveR, wantR)) {
					g_tracked.erase(a_formID);
					return;
				}
				const float factor = ScaleMath::RadiusScaleFactor(liveR, wantR);
				if (factor == 0.0f || !ScaleControllerXY(actor.get(), controller, factor)) {
					if (g_tracked.size() > kMaxTracked) {
						g_tracked.erase(a_formID);
					}
					return;
				}
				g_tracked.erase(a_formID);
				return;
			}

			if (!SetActorScale(actor.get(), ScaleMath::kVanilla)) {
				if (g_tracked.size() > kMaxTracked) {
					g_tracked.erase(a_formID);
				}
				return;
			}
			g_tracked.erase(a_formID);
		}

		RE::NiPoint3 HkToWorld(const RE::hkVector4& a_vec, const RE::NiPoint3& a_origin, float a_invScale)
		{
			return {
				a_origin.x + a_vec.quad.m128_f32[0] * a_invScale,
				a_origin.y + a_vec.quad.m128_f32[1] * a_invScale,
				a_origin.z + a_vec.quad.m128_f32[2] * a_invScale
			};
		}

		void DrawHullLines(
			TRUEHUD_API::IVTrueHUD3* a_hud,
			const std::vector<RE::NiPoint3>& a_pts,
			const std::vector<int>& a_loop,
			std::uint32_t a_color)
		{
			if (a_loop.size() < 2) {
				return;
			}
			for (std::size_t i = 0; i + 1 < a_loop.size(); ++i) {
				const int a = a_loop[i];
				const int b = a_loop[i + 1];
				if (a < 0 || b < 0 || a >= static_cast<int>(a_pts.size()) ||
					b >= static_cast<int>(a_pts.size())) {
					continue;
				}
				a_hud->DrawLine(
					a_pts[static_cast<std::size_t>(a)],
					a_pts[static_cast<std::size_t>(b)],
					0.0f,
					a_color,
					1.5f);
			}
		}

		void CopyLiveConvexVertices(
			RE::hkpConvexVerticesShape* a_convex,
			std::vector<RE::hkVector4>& a_out)
		{
			a_out.clear();
			if (!a_convex || a_convex->numVertices <= 0 || a_convex->rotatedVertices.empty()) {
				return;
			}
			const auto target = static_cast<std::size_t>(a_convex->numVertices);
			a_out.reserve(target);
			for (const auto& ring : a_convex->rotatedVertices) {
				for (int component = 0; component < 4; ++component) {
					if (a_out.size() >= target) {
						return;
					}
					RE::hkVector4 vert{};
					vert.quad.m128_f32[0] = ring.x.quad.m128_f32[component];
					vert.quad.m128_f32[1] = ring.y.quad.m128_f32[component];
					vert.quad.m128_f32[2] = ring.z.quad.m128_f32[component];
					a_out.push_back(vert);
				}
			}
		}

		float AverageLoopZ(const std::vector<RE::NiPoint3>& a_pts, const std::vector<int>& a_loop)
		{
			float sum = 0.0f;
			int count = 0;
			for (const int idx : a_loop) {
				if (idx < 0 || idx >= static_cast<int>(a_pts.size())) {
					continue;
				}
				sum += a_pts[static_cast<std::size_t>(idx)].z;
				++count;
			}
			return count > 0 ? sum / static_cast<float>(count) : 0.0f;
		}

		void DrawZBandLoop(
			TRUEHUD_API::IVTrueHUD3* a_hud,
			const std::vector<RE::NiPoint3>& a_pts,
			const RE::NiPoint3& a_origin,
			float a_bandZ,
			float a_bandTol,
			std::uint32_t a_color)
		{
			if (!a_hud || a_pts.empty()) {
				return;
			}

			std::vector<int> band;
			band.reserve(a_pts.size());
			for (int i = 0; i < static_cast<int>(a_pts.size()); ++i) {
				if (std::fabs(a_pts[static_cast<std::size_t>(i)].z - a_bandZ) <= a_bandTol) {
					band.push_back(i);
				}
			}
			if (band.size() < 3) {
				return;
			}

			std::sort(band.begin(), band.end(), [&](int a_lhs, int a_rhs) {
				const auto& lp = a_pts[static_cast<std::size_t>(a_lhs)];
				const auto& rp = a_pts[static_cast<std::size_t>(a_rhs)];
				const float la = std::atan2(lp.y - a_origin.y, lp.x - a_origin.x);
				const float ra = std::atan2(rp.y - a_origin.y, rp.x - a_origin.x);
				return la < ra;
			});

			std::vector<int> loop;
			loop.reserve(band.size() + 1);
			for (const int idx : band) {
				loop.push_back(idx);
			}
			loop.push_back(band.front());
			DrawHullLines(a_hud, a_pts, loop, a_color);
		}

		void DrawActorHull(TRUEHUD_API::IVTrueHUD3* a_hud, RE::Actor* a_actor, bool a_log)
		{
			if (!a_hud || !a_actor) {
				return;
			}
			auto* controller = a_actor->GetCharController();
			auto* cell = a_actor->GetParentCell();
			if (!controller || !cell) {
				return;
			}
			auto* world = cell->GetbhkWorld();
			auto* body = controller->GetBodyImpl();
			if (!world || !body) {
				return;
			}

			const float inv = RE::bhkWorld::GetWorldScaleInverse();
			RE::hkVector4 hkPos;
			controller->GetPosition(hkPos, false);
			const RE::NiPoint3 origin{
				hkPos.quad.m128_f32[0] * inv,
				hkPos.quad.m128_f32[1] * inv,
				hkPos.quad.m128_f32[2] * inv
			};

			RE::hkpListShape* list = nullptr;
			RE::hkpConvexVerticesShape* convex = nullptr;
			std::vector<RE::hkVector4> liveVerts{};
			RE::hkArray<RE::hkVector4> verts{};
			{
				RE::BSReadLockGuard lock(world->worldLock);
				if (!FindConvex(body, list, convex)) {
					return;
				}
				CopyLiveConvexVertices(convex, liveVerts);
				if (!liveVerts.empty()) {
					verts.resize(static_cast<RE::hkArray<RE::hkVector4>::size_type>(liveVerts.size()));
					for (std::size_t i = 0; i < liveVerts.size(); ++i) {
						verts[static_cast<RE::hkArray<RE::hkVector4>::size_type>(i)] = liveVerts[i];
					}
				} else {
					hkpConvexVerticesShape_getOriginalVertices(convex, verts);
				}
			}

			if (verts.size() <= 0) {
				return;
			}

			std::vector<RE::NiPoint3> pts(static_cast<std::size_t>(verts.size()));
			float maxR = 0.0f;
			float waistZ = origin.z;
			float waistBandTol = 8.0f * inv;
			for (RE::hkArray<RE::hkVector4>::size_type i = 0; i < verts.size(); ++i) {
				pts[static_cast<std::size_t>(i)] = HkToWorld(verts[i], origin, inv);
				const float x = verts[i].quad.m128_f32[0];
				const float y = verts[i].quad.m128_f32[1];
				const float r = std::sqrt(x * x + y * y) * inv;
				if (r > maxR) {
					maxR = r;
				}
			}
			if (verts.size() == 18) {
				const std::vector<int> kUpperLoop{ 1, 4, 13, 7, 3, 16, 5, 11 };
				const std::vector<int> kLowerLoop{ 0, 2, 12, 6, 15, 17, 14, 10 };
				const float upperZ = AverageLoopZ(pts, kUpperLoop);
				const float lowerZ = AverageLoopZ(pts, kLowerLoop);
				waistZ = (upperZ + lowerZ) * 0.5f;
				waistBandTol = std::max(8.0f * inv, std::fabs(upperZ - lowerZ) * 0.18f);
			}

			constexpr std::uint32_t kGreen = 0x00FF00FF;
			if (verts.size() == 18) {
				DrawZBandLoop(a_hud, pts, origin, waistZ, waistBandTol, kGreen);
			} else {
				a_hud->DrawCircle(
					origin,
					RE::NiPoint3{ 1.0f, 0.0f, 0.0f },
					RE::NiPoint3{ 0.0f, 1.0f, 0.0f },
					maxR,
					24,
					0.0f,
					kGreen,
					2.0f);
			}

			if (verts.size() == 18) {
				DrawHullLines(a_hud, pts, { 1, 4, 13, 7, 3, 16, 5, 11, 1 }, kGreen);
				DrawHullLines(a_hud, pts, { 0, 2, 12, 6, 15, 17, 14, 10, 0 }, kGreen);
			}

			if (a_log) {
				static std::uint32_t logFrames = 0;
				if (++logFrames >= 120) {
					logFrames = 0;
					float applied = ScaleMath::kVanilla;
					if (const auto it = g_tracked.find(a_actor->GetFormID()); it != g_tracked.end()) {
						applied = it->second.applied;
					}
					logger::debug(
						"debug hull xyRadius={:.1f} applied={:.2f} verts={}",
						maxR,
						applied,
						verts.size());
				}
			}
		}

		void DrawDebug()
		{
			if (!Settings::bDebugDraw) {
				return;
			}
			auto* hud = TRUEHUD::GetInterface();
			if (!hud) {
				return;
			}

			std::unordered_set<RE::FormID> drawn;
			if (auto* player = RE::PlayerCharacter::GetSingleton()) {
				DrawActorHull(hud, player, true);
				drawn.insert(player->GetFormID());
			}

			std::uint32_t extra = 0;
			for (const auto& [id, tracked] : g_tracked) {
				if (drawn.contains(id) || extra >= 7) {
					continue;
				}
				if (const auto actor = tracked.handle.get()) {
					DrawActorHull(hud, actor.get(), false);
					++extra;
				}
			}
		}

		Tracked* FindTrackedByController(RE::bhkCharacterController* a_controller)
		{
			if (!a_controller) {
				return nullptr;
			}
			for (auto& [id, tracked] : g_tracked) {
				if (tracked.controller == a_controller) {
					return &tracked;
				}
			}
			return nullptr;
		}

		void FilterWorldSolverConstraints(
			RE::bhkCharProxyController* a_self,
			const RE::hkpCharacterProxy* a_proxy,
			const RE::hkArray<RE::hkpRootCdPoint>& a_manifold,
			RE::hkpSimplexSolverInput& a_input)
		{
			if (!Settings::bEnabled || !Settings::bShrinkWhenPinched) {
				return;
			}
			if (!a_self || !a_proxy || !a_proxy->shapePhantom || !a_input.constraints) {
				return;
			}
			if (a_input.numConstraints <= 0 || a_manifold.empty()) {
				return;
			}

			auto* controller = static_cast<RE::bhkCharacterController*>(a_self);
			auto* tracked = FindTrackedByController(controller);
			if (!tracked || tracked->slideActive ||
				!ScaleMath::NeedsApply(tracked->applied, ScaleMath::kVanilla)) {
				return;
			}

			const auto* selfCol = static_cast<const RE::hkpCollidable*>(&a_proxy->shapePhantom->collidable);
			const auto& translation = a_proxy->shapePhantom->motionState.transform.translation;
			const float ox = translation.quad.m128_f32[0];
			const float oy = translation.quad.m128_f32[1];
			const float vanillaHk = kHumanXYRadius * RE::bhkWorld::GetWorldScale();
			const float keepDistance = a_proxy->keepDistance;
			const auto manifoldCount = a_manifold.size();
			const auto limit = manifoldCount < a_input.numConstraints ?
				manifoldCount :
				a_input.numConstraints;

			for (RE::hkArray<RE::hkpRootCdPoint>::size_type i = 0; i < limit; ++i) {
				const auto& pt = a_manifold[i];
				const RE::hkpCollidable* other = ManifoldOtherCollidable(pt, selfCol);
				if (!other || IsActorCollisionLayer(other->GetCollisionLayer())) {
					continue;
				}

				const float nx = pt.contact.separatingNormal.quad.m128_f32[0];
				const float ny = pt.contact.separatingNormal.quad.m128_f32[1];
				const float nxy2 = nx * nx + ny * ny;
				if (nxy2 < 0.35f * 0.35f) {
					continue;
				}
				const float inv = 1.0f / std::sqrt(nxy2);
				const float nxx = nx * inv;
				const float nyy = ny * inv;
				const float cx = pt.contact.position.quad.m128_f32[0];
				const float cy = pt.contact.position.quad.m128_f32[1];
				float alongHk = nxx * (ox - cx) + nyy * (oy - cy);
				if (alongHk < 0.0f) {
					alongHk = -alongHk;
				}

				a_input.constraints[i].plane.quad.m128_f32[3] =
					WallClip::VanillaWorldStopDistanceHk(alongHk, vanillaHk, keepDistance);
			}

			const float combatHk = vanillaHk * (tracked->applied > 1.0f ? tracked->applied : 1.0f);
			for (std::int32_t i = static_cast<std::int32_t>(limit); i < a_input.numConstraints; ++i) {
				auto& constraint = a_input.constraints[i];
				const float nx = constraint.plane.quad.m128_f32[0];
				const float ny = constraint.plane.quad.m128_f32[1];
				const float nxy2 = nx * nx + ny * ny;
				if (nxy2 < 0.35f * 0.35f) {
					continue;
				}
				float alongHk = combatHk + (constraint.plane.quad.m128_f32[3] + keepDistance);
				if (alongHk < 0.0f) {
					alongHk = -alongHk;
				}
				constraint.plane.quad.m128_f32[3] =
					WallClip::VanillaWorldStopDistanceHk(alongHk, vanillaHk, keepDistance);
			}
		}

		REL::Relocation<void(RE::bhkCharProxyController*, const RE::hkpCharacterProxy*, const RE::hkArray<RE::hkpRootCdPoint>&, RE::hkpSimplexSolverInput&)>
			_ProcessConstraints;

		void ProcessConstraintsThunk(
			RE::bhkCharProxyController* a_self,
			const RE::hkpCharacterProxy* a_proxy,
			const RE::hkArray<RE::hkpRootCdPoint>& a_manifold,
			RE::hkpSimplexSolverInput& a_input)
		{
			_ProcessConstraints(a_self, a_proxy, a_manifold, a_input);
			FilterWorldSolverConstraints(a_self, a_proxy, a_manifold, a_input);
		}

		void InstallProxyHooksInternal()
		{
			REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_bhkCharProxyController[0] };
			_ProcessConstraints = vtbl.write_vfunc(1, ProcessConstraintsThunk);
			logger::info("Installed world-contact filter on bhkCharProxyController");
		}
	}

	void Reset()
	{
		std::vector<RE::FormID> ids;
		ids.reserve(g_tracked.size());
		for (const auto& [id, _] : g_tracked) {
			ids.push_back(id);
		}
		if (!ids.empty()) {
			logger::debug("collision reset restoring {} tracked actor(s)", ids.size());
		}
		for (const auto id : ids) {
			RestoreForm(id);
		}
		g_tracked.clear();
		g_vcdBase.clear();
		g_postLoadSanitize = true;
		ClearSlideSession();
	}

	void SetVcdFightOverride(bool a_present)
	{
		g_vcdFightOverride = a_present;
	}

	void SetSkyParkourPresent(bool a_present)
	{
		g_skyParkourPresent = a_present;
		if (!a_present) {
			ClearSlideSession();
		}
	}

	void InstallProxyHooks()
	{
		InstallProxyHooksInternal();
	}

	void Update()
	{
		Settings::ReloadIfChanged();

		auto* ui = RE::UI::GetSingleton();
		if (ui && ui->GameIsPaused()) {
			return;
		}

		DrawDebug();

		static std::uint32_t frames = 0;
		bool fastCombatTick = false;
		if (auto* player = RE::PlayerCharacter::GetSingleton()) {
			if (player->IsInCombat()) {
				fastCombatTick = true;
			} else if (Settings::bAllyCombatCollision) {
				if (const auto* lists = RE::ProcessLists::GetSingleton()) {
					for (auto& handle : lists->highActorHandles) {
						const auto actor = handle.get();
						if (actor && actor->IsPlayerTeammate() && actor->IsInCombat()) {
							fastCombatTick = true;
							break;
						}
					}
				}
			} else if (const auto it = g_tracked.find(player->GetFormID()); it != g_tracked.end()) {
				fastCombatTick = it->second.slideActive;
			}
			if (!fastCombatTick && PlayerNeedsSlideTrack(player)) {
				fastCombatTick = true;
			}
		}
		const auto interval = fastCombatTick ? kCombatUpdateInterval :
											 (Settings::bLockTargetOnly ? 1u : kUpdateInterval);
		if (++frames < interval) {
			return;
		}
		frames = 0;

		std::unordered_set<RE::FormID> wanted;
		CollectWanted(wanted);

		if (g_vcdFightOverride) {
			if (auto* cachePlayer = RE::PlayerCharacter::GetSingleton()) {
				if (!wanted.contains(cachePlayer->GetFormID())) {
					TryCacheVcdBase(cachePlayer);
				}
			}
		}

		std::vector<RE::FormID> toRestore;
		toRestore.reserve(g_tracked.size());
		for (const auto& [id, _] : g_tracked) {
			if (!wanted.contains(id)) {
				toRestore.push_back(id);
			}
		}
		for (const auto id : toRestore) {
			RestoreForm(id);
		}

		const float combatScale = Settings::bEnabled ?
			ScaleForPlayerWeapon(RE::PlayerCharacter::GetSingleton()) :
			ScaleMath::kVanilla;
		auto* player = RE::PlayerCharacter::GetSingleton();
		for (const auto id : wanted) {
			auto* actor = RE::TESForm::LookupByID<RE::Actor>(id);
			if (!ActorUsable(actor)) {
				RestoreForm(id);
				continue;
			}
			const float target = Settings::bEnabled ?
				ScaleForActor(actor, player, combatScale) :
				ScaleMath::kVanilla;
			SetActorScale(actor, target);
		}
	}
}
