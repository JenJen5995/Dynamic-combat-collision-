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
		constexpr float kCrouchHullScale = 0.5f;
		constexpr const char* kSkyParkourSliding = "SkyParkourSliding";
		constexpr const char* kSkyParkourIsLandingRoll = "SkyParkourIsLandingRoll";

		enum class LowHull : std::uint8_t
		{
			None,
			Crouch,
			Slide
		};
		constexpr float kMinVanillaXY = 12.0f;
		constexpr float kMaxVanillaXY = 24.0f;
		constexpr float kMaxVcdXY = 96.0f;
		constexpr float kMaxApplied = 8.00f;
		constexpr float kHullCloseGapWorld = 250.0f;
		constexpr float kNpcFightRangeWorld = 512.0f;
		constexpr float kHullCloseEpsWorld = 2.0f;
		constexpr float kHullCloseSpeedWorld = 180.0f;
		constexpr float kHullCloseStepSkip = 0.35f;

		struct HullSnapshot
		{
			std::vector<RE::hkFourTransposedPoints> rotatedVertices;
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
			LowHull poseShrink{ LowHull::None };
			std::uint32_t closeFrame{ 0 };
			float closeLastX{ 0.0f };
			float closeLastY{ 0.0f };
		};

		std::uint32_t g_hullCloseFrame = 0;
		std::unordered_map<RE::FormID, Tracked> g_tracked;
		std::unordered_map<RE::FormID, float> g_vcdBase;
		struct PhantomScale
		{
			const RE::hkpCharacterProxy* proxy{ nullptr };
			float slider{ ScaleMath::kVanilla };
		};
		PhantomScale g_phantomScales[kMaxActors]{};
		std::uint32_t g_phantomCount = 0;

		struct ThunkHull
		{
			const RE::hkpCharacterProxy* proxy{ nullptr };
			float vanillaR{ 0.0f };
			float applied{ ScaleMath::kVanilla };
			float worldX{ 0.0f };
			float worldY{ 0.0f };
			float worldZ{ 0.0f };
			float liveR{ 0.0f };
			bool isPlayer{ false };
		};
		ThunkHull g_thunkHulls[kMaxActors]{};
		float g_thunkAppliedThisCall = ScaleMath::kVanilla;
		bool g_vcdFightOverride = false;
		bool g_skyParkourPresent = false;
		bool g_slideSession = false;
		bool g_slideFromGround = false;
		bool g_rollArmed = false;
		bool g_postLoadSanitize = false;
		bool g_playerMountHold = false;

		LowHull DesiredLowHull(RE::Actor* a_actor);
		bool FindConvexList(
			RE::bhkCharacterController* a_controller,
			RE::hkpListShape*& a_list,
			RE::hkpConvexVerticesShape*& a_convex);
		float MeasureConvexXYWorld(RE::hkpConvexVerticesShape* a_convex);
		bool ActorIsGone(RE::Actor* a_actor)
		{
			if (!a_actor) {
				return true;
			}
			if (auto* state = a_actor->AsActorState()) {
				switch (state->GetLifeState()) {
				case RE::ACTOR_LIFE_STATE::kDead:
				case RE::ACTOR_LIFE_STATE::kDying:
				case RE::ACTOR_LIFE_STATE::kRecycle:
					return true;
				default:
					return false;
				}
			}
			return a_actor->IsDead(false);
		}

		bool ActorUsable(RE::Actor* a_actor)
		{
			if (!a_actor || a_actor->IsDisabled() || a_actor->IsDeleted() || !a_actor->Is3DLoaded()) {
				return false;
			}
			if (ActorIsGone(a_actor)) {
				return false;
			}
			return a_actor->GetCharController() != nullptr;
		}

		bool ActorHullReady(RE::Actor* a_actor)
		{
			return a_actor && !ActorIsGone(a_actor) && a_actor->GetCharController() != nullptr;
		}

		bool ControllerXYWorld(RE::Actor* a_actor, float& a_x, float& a_y)
		{
			auto* ctrl = a_actor ? a_actor->GetCharController() : nullptr;
			if (!ctrl) {
				return false;
			}
			RE::hkVector4 hk;
			ctrl->GetPosition(hk, false);
			const float inv = RE::bhkWorld::GetWorldScaleInverse();
			a_x = hk.quad.m128_f32[0] * inv;
			a_y = hk.quad.m128_f32[1] * inv;
			return true;
		}

		bool ActorsWithinFightRange(RE::Actor* a_a, RE::Actor* a_b)
		{
			float ax = 0.0f;
			float ay = 0.0f;
			float bx = 0.0f;
			float by = 0.0f;
			if (!ControllerXYWorld(a_a, ax, ay) || !ControllerXYWorld(a_b, bx, by)) {
				return false;
			}
			const float dx = ax - bx;
			const float dy = ay - by;
			return (dx * dx) + (dy * dy) <= kNpcFightRangeWorld * kNpcFightRangeWorld;
		}

		bool PhantomsWithinFightRange(
			const RE::hkpCharacterProxy* a_a,
			const RE::hkpCharacterProxy* a_b)
		{
			if (!a_a || !a_b || !a_a->shapePhantom || !a_b->shapePhantom) {
				return false;
			}
			const auto& ta = a_a->shapePhantom->motionState.transform.translation;
			const auto& tb = a_b->shapePhantom->motionState.transform.translation;
			const float inv = RE::bhkWorld::GetWorldScaleInverse();
			const float dx = (ta.quad.m128_f32[0] - tb.quad.m128_f32[0]) * inv;
			const float dy = (ta.quad.m128_f32[1] - tb.quad.m128_f32[1]) * inv;
			return (dx * dx) + (dy * dy) <= kNpcFightRangeWorld * kNpcFightRangeWorld;
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

		bool PlayerMountBlocksCombat(RE::Actor* a_player);

		bool ShouldScaleHull(RE::Actor* a_actor)
		{
			if (!a_actor) {
				return false;
			}
			if (a_actor->IsAMount()) {
				return false;
			}
			if (a_actor->IsPlayerRef()) {
				return !PlayerMountBlocksCombat(a_actor);
			}

			auto* race = a_actor->GetRace();
			const auto hasKw = [&](std::string_view a_kw) {
				if (a_actor->HasKeywordString(a_kw)) {
					return true;
				}
				return race && race->HasKeywordString(a_kw);
			};
			if (hasKw("ActorTypeNPC"sv)) {
				return true;
			}
			if (hasKw("ActorTypeAnimal"sv) || hasKw("ActorTypeCreature"sv) ||
				hasKw("ActorTypeHorse"sv) || hasKw("ActorTypeDragon"sv) ||
				hasKw("ActorTypePrey"sv) || hasKw("ActorTypePredator"sv) ||
				hasKw("ActorTypeCritter"sv)) {
				return false;
			}
			if (race && IsBigfootRace(race)) {
				return true;
			}
			return a_actor->GetActorBase() != nullptr && a_actor->GetCharController() != nullptr;
		}

		void ClearSlideSession()
		{
			g_slideSession = false;
			g_slideFromGround = false;
		}

		void ClearRollArmed()
		{
			g_rollArmed = false;
		}

		bool PlayerOnFootGround(RE::Actor* a_actor)
		{
			if (!a_actor || a_actor->IsInMidair()) {
				return false;
			}
			const auto* ctrl = a_actor->GetCharController();
			if (!ctrl) {
				return true;
			}
			return ctrl->context.currentState == RE::hkpCharacterStateType::kOnGround;
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

		bool IsSkyParkourLandingRoll(RE::Actor* a_actor)
		{
			if (!g_skyParkourPresent || !a_actor || !a_actor->IsPlayerRef()) {
				return false;
			}

			bool isRoll = false;
			a_actor->GetGraphVariableBool(kSkyParkourIsLandingRoll, isRoll);
			const bool onGround = PlayerOnFootGround(a_actor);

			if (isRoll && !onGround) {
				g_rollArmed = true;
				return false;
			}

			bool sliding = false;
			a_actor->GetGraphVariableBool(kSkyParkourSliding, sliding);

			if (g_rollArmed) {
				if (!onGround) {
					return false;
				}
				if (sliding || isRoll) {
					return true;
				}
				ClearRollArmed();
				return false;
			}

			return isRoll && onGround;
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
			return DesiredLowHull(a_player) != LowHull::None || PlayerSlideActive(a_player);
		}

		bool PlayerMountBlocksCombat(RE::Actor* a_player)
		{
			if (!a_player || !a_player->IsPlayerRef()) {
				return false;
			}

			RE::NiPointer<RE::Actor> mount;
			const bool onMount = a_player->GetMount(mount) && mount;
			if (onMount) {
				g_playerMountHold = true;
				return true;
			}

			auto* state = a_player->AsActorState();
			const auto sit = state ? state->GetSitSleepState() : RE::SIT_SLEEP_STATE::kNormal;
			if (g_playerMountHold) {
				if (sit != RE::SIT_SLEEP_STATE::kNormal || a_player->IsOnMount()) {
					return true;
				}
				g_playerMountHold = false;
			}
			return false;
		}

		bool PlayerCombatHullMode(RE::Actor* a_player)
		{
			if (!Settings::bEnabled || !a_player) {
				return false;
			}
			if (PlayerMountBlocksCombat(a_player)) {
				return false;
			}
			if (Settings::bLockTargetOnly) {
				const auto* tdm = TDM_API::GetInterface();
				return tdm && tdm->GetTargetLockState();
			}
			return a_player->IsInCombat();
		}

		LowHull DesiredLowHull(RE::Actor* a_actor)
		{
			if (!a_actor || !a_actor->IsPlayerRef()) {
				return LowHull::None;
			}
			if (PlayerMountBlocksCombat(a_actor)) {
				return LowHull::None;
			}
			if (IsSkyParkourLandingRoll(a_actor) || IsSkyParkourCrouchSlide(a_actor)) {
				return LowHull::Slide;
			}
			if (PlayerCombatHullMode(a_actor) && a_actor->IsSneaking()) {
				return LowHull::Crouch;
			}
			return LowHull::None;
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

		bool CombatTargetIs(RE::Actor* a_actor, RE::Actor* a_other)
		{
			if (!a_actor || !a_other) {
				return false;
			}
			const auto target = a_actor->GetActorRuntimeData().currentCombatTarget.get();
			return target.get() == a_other;
		}

		bool HasCombatTarget(RE::Actor* a_actor)
		{
			if (!a_actor) {
				return false;
			}
			return a_actor->GetActorRuntimeData().currentCombatTarget.get().get() != nullptr;
		}

		bool InPlayersFight(RE::Actor* a_actor, RE::Actor* a_player)
		{
			if (!a_actor || !a_player) {
				return false;
			}
			if (a_actor == a_player) {
				return a_player->IsInCombat();
			}
			if (a_actor->IsAMount()) {
				return false;
			}

			if (CombatTargetIs(a_player, a_actor) || CombatTargetIs(a_actor, a_player)) {
				return true;
			}
			if (!a_player->IsInCombat()) {
				return false;
			}
			if (a_actor->IsPlayerTeammate()) {
				return HasCombatTarget(a_actor) || ActorsWithinFightRange(a_actor, a_player);
			}
			return HasCombatTarget(a_actor) || ActorsWithinFightRange(a_actor, a_player);
		}

		bool InAllyFight(RE::Actor* a_actor, RE::Actor* a_ally)
		{
			if (!a_actor || !a_ally || a_actor == a_ally) {
				return false;
			}
			if (a_actor->GetCurrentScene() || a_ally->GetCurrentScene()) {
				return false;
			}

			return CombatTargetIs(a_actor, a_ally) || CombatTargetIs(a_ally, a_actor) ||
				HasCombatTarget(a_actor) ||
				ActorsWithinFightRange(a_actor, a_ally);
		}

		void CollectAllyFights(
			const std::function<void(RE::Actor*)>& a_add,
			const RE::ProcessLists* a_lists);

		void ForEachNearbyLoadedActor(
			RE::Actor* a_player,
			const std::function<void(RE::Actor*)>& a_fn)
		{
			if (!a_player || !a_fn) {
				return;
			}

			RE::FormID seen[kMaxActors]{};
			std::uint32_t seenCount = 0;
			const auto consider = [&](RE::Actor* actor) {
				if (!actor || actor == a_player) {
					return;
				}
				const auto id = actor->GetFormID();
				for (std::uint32_t i = 0; i < seenCount; ++i) {
					if (seen[i] == id) {
						return;
					}
				}
				if (seenCount < kMaxActors) {
					seen[seenCount++] = id;
				}
				if (!ActorsWithinFightRange(actor, a_player)) {
					return;
				}
				a_fn(actor);
			};

			if (auto* tes = RE::TES::GetSingleton()) {
				tes->ForEachReferenceInRange(a_player, kNpcFightRangeWorld, [&](RE::TESObjectREFR* ref) {
					consider(ref ? ref->As<RE::Actor>() : nullptr);
					return RE::BSContainer::ForEachResult::kContinue;
				});
			}

			auto* lists = RE::ProcessLists::GetSingleton();
			if (!lists) {
				return;
			}
			const RE::BSTArray<RE::ActorHandle>* buckets[] = {
				&lists->highActorHandles,
				&lists->middleHighActorHandles,
				&lists->middleLowActorHandles
			};
			for (const auto* bucket : buckets) {
				if (!bucket) {
					continue;
				}
				for (auto& handle : *bucket) {
					consider(handle.get().get());
				}
			}
		}

		void ForEachFightNpc(RE::Actor* a_player, const std::function<void(RE::Actor*)>& a_fn)
		{
			if (!a_player || !a_fn) {
				return;
			}
			ForEachNearbyLoadedActor(a_player, [&](RE::Actor* actor) {
				if (!ActorHullReady(actor) || !ShouldScaleHull(actor)) {
					return;
				}
				if (InPlayersFight(actor, a_player)) {
					static RE::FormID loggedNpc = 0;
					if (loggedNpc == 0) {
						loggedNpc = actor->GetFormID();
						logger::info(
							"npc hull select {:08X} name='{}'",
							loggedNpc,
							actor->GetName());
					}
					a_fn(actor);
				}
			});
			if (Settings::bAllyCombatCollision) {
				if (auto* lists = RE::ProcessLists::GetSingleton()) {
					CollectAllyFights(
						[&](RE::Actor* a_actor) {
							if (ActorHullReady(a_actor) && ShouldScaleHull(a_actor)) {
								a_fn(a_actor);
							}
						},
						lists);
				}
			}
		}

		void CollectAllyFights(
			const std::function<void(RE::Actor*)>& a_add,
			const RE::ProcessLists* a_lists)
		{
			if (!a_lists) {
				return;
			}

			auto* player = RE::PlayerCharacter::GetSingleton();
			RE::Actor* allies[kMaxActors]{};
			std::uint32_t allyCount = 0;
			for (auto& handle : a_lists->highActorHandles) {
				const auto actor = handle.get();
				if (!actor || !actor->IsPlayerTeammate()) {
					continue;
				}
				const bool inFight = HasCombatTarget(actor.get()) ||
					(player && player->IsInCombat() && ActorsWithinFightRange(actor.get(), player));
				if (!inFight) {
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
				if (!actor) {
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
				if (!ActorHullReady(a_actor) || !ShouldScaleHull(a_actor)) {
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
					ForEachFightNpc(player, add);
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
			for (RE::hkArray<RE::hkFourTransposedPoints>::size_type i = 0; i < rings; ++i) {
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
			for (RE::hkArray<RE::hkFourTransposedPoints>::size_type i = 0; i < rings; ++i) {
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
			for (RE::hkArray<RE::hkFourTransposedPoints>::size_type i = 0; i < rings; ++i) {
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
			std::vector<RE::hkFourTransposedPoints>& a_out,
			const RE::hkArray<RE::hkFourTransposedPoints>& a_in)
		{
			a_out.resize(static_cast<std::size_t>(a_in.size()));
			for (RE::hkArray<RE::hkFourTransposedPoints>::size_type i = 0; i < a_in.size(); ++i) {
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
			RE::hkArray<RE::hkFourTransposedPoints>& a_out,
			const std::vector<RE::hkFourTransposedPoints>& a_in)
		{
			a_out.resize(static_cast<RE::hkArray<RE::hkFourTransposedPoints>::size_type>(a_in.size()));
			for (std::size_t i = 0; i < a_in.size(); ++i) {
				a_out[static_cast<RE::hkArray<RE::hkFourTransposedPoints>::size_type>(i)] =
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

		RE::bhkCharProxyController* AsCharProxyController(RE::bhkCharacterController* a_controller)
		{
			if (!a_controller) {
				return nullptr;
			}
			if (auto* proxy = skyrim_cast<RE::bhkCharProxyController*>(a_controller)) {
				return proxy;
			}

			const auto vtbl = *reinterpret_cast<const std::uintptr_t*>(a_controller);
			static const auto listenerVtbl = RE::VTABLE_bhkCharProxyController[0].address();
			static const auto ccVtbl = RE::VTABLE_bhkCharProxyController[1].address();
			constexpr std::uintptr_t kListenerToCC = 0x10;
			if (vtbl == listenerVtbl) {
				return reinterpret_cast<RE::bhkCharProxyController*>(a_controller);
			}
			if (vtbl == ccVtbl) {
				static bool logged = false;
				if (!logged) {
					logged = true;
					logger::info("proxy controller recovered from CC vtable (GetCharController is the subobject)");
				}
				return reinterpret_cast<RE::bhkCharProxyController*>(
					reinterpret_cast<std::uintptr_t>(a_controller) - kListenerToCC);
			}
			return nullptr;
		}

		const RE::hkpCollidable* ProxyCollidable(RE::bhkCharacterController* a_controller)
		{
			auto* proxyCtrl = AsCharProxyController(a_controller);
			if (!proxyCtrl) {
				return nullptr;
			}
			auto* proxy = proxyCtrl->GetCharacterProxy();
			if (!proxy || !proxy->shapePhantom) {
				return nullptr;
			}
			return static_cast<const RE::hkpCollidable*>(&proxy->shapePhantom->collidable);
		}

		const RE::hkpCollidable* PlayerProxyCollidable()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) {
				return nullptr;
			}
			return ProxyCollidable(player->GetCharController());
		}

		RE::hkpCharacterProxy* PlayerCharacterProxy()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) {
				return nullptr;
			}
			auto* ctrl = AsCharProxyController(player->GetCharController());
			return ctrl ? ctrl->GetCharacterProxy() : nullptr;
		}

		bool IsPlayerCollidable(const RE::hkpCollidable* a_col)
		{
			if (!a_col) {
				return false;
			}
			if (a_col == PlayerProxyCollidable()) {
				return true;
			}
			auto* refr = RE::TESHavokUtilities::FindCollidableRef(*a_col);
			return refr && refr->IsPlayerRef();
		}

		float ActorRadiusHk(const Tracked* a_tracked)
		{
			const float world = a_tracked && a_tracked->vanillaRadius > 0.0f ?
				a_tracked->vanillaRadius * (a_tracked->applied > 0.0f ? a_tracked->applied : ScaleMath::kVanilla) :
				kHumanXYRadius;
			return world * RE::bhkWorld::GetWorldScale();
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

		RE::hkpWorldObject* CharacterHullObject(RE::bhkCharacterController* a_controller)
		{
			if (!a_controller) {
				return nullptr;
			}
			if (auto* proxyCtrl = AsCharProxyController(a_controller)) {
				if (auto* proxy = proxyCtrl->GetCharacterProxy(); proxy && proxy->shapePhantom) {
					return proxy->shapePhantom;
				}
			}
			return a_controller->GetRigidBody();
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
			auto* body = CharacterHullObject(a_controller);
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

			const float inv = RE::bhkWorld::GetWorldScaleInverse();
			float maxR = 0.0f;
			if (a_convex->numVertices > 0 && !a_convex->rotatedVertices.empty()) {
				const auto target = static_cast<std::size_t>(a_convex->numVertices);
				std::size_t n = 0;
				const auto rings = a_convex->rotatedVertices.size();
				for (RE::hkArray<RE::hkFourTransposedPoints>::size_type i = 0; i < rings; ++i) {
					const auto& ring = a_convex->rotatedVertices[i];
					for (int component = 0; component < 4; ++component) {
						if (n >= target) {
							break;
						}
						const float x = ring.x.quad.m128_f32[component];
						const float y = ring.y.quad.m128_f32[component];
						const float r = std::sqrt(x * x + y * y) * inv;
						if (r > maxR) {
							maxR = r;
						}
						++n;
					}
					if (n >= target) {
						break;
					}
				}
				if (maxR > 0.0f) {
					return maxR;
				}
			}

			RE::hkArray<RE::hkVector4> verts{};
			hkpConvexVerticesShape_getOriginalVertices(a_convex, verts);
			if (verts.size() <= 0) {
				return 0.0f;
			}

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

			auto* body = CharacterHullObject(a_controller);
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

			auto* body = CharacterHullObject(a_controller);
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
			a_tracked.poseShrink = LowHull::None;
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

		bool ApplyLowHullShrink(
			RE::Actor* a_actor,
			RE::bhkCharacterController* a_controller,
			Tracked& a_tracked,
			LowHull a_kind)
		{
			if (!a_actor || !a_controller || a_kind == LowHull::None) {
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
			if (a_kind == LowHull::Crouch &&
				a_tracked.slideActive &&
				a_tracked.poseShrink == LowHull::Crouch) {
				return true;
			}

			const float wantXY = a_kind == LowHull::Slide ?
				std::max(kMinSlideXYWorld, kSlideXYWorld) :
				liveXY * kCrouchHullScale;
			const float wantH = a_kind == LowHull::Slide ?
				std::max(kMinSlideHeightWorld, kSlideHeightWorld) :
				liveH * kCrouchHullScale;
			const float sx = ScaleMath::ShrinkFactor(liveXY, wantXY);
			const float sz = ScaleMath::ShrinkFactor(liveH, wantH);
			const bool needXY = ScaleMath::NeedsApply(sx, 1.0f);
			const bool needZ = ScaleMath::NeedsApply(sz, 1.0f);
			if (a_tracked.slideActive && !needXY && !needZ) {
				a_tracked.poseShrink = a_kind;
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

			if (!a_tracked.slideActive || a_tracked.poseShrink != a_kind) {
				a_tracked.slideActive = true;
				a_tracked.poseShrink = a_kind;
				logger::debug(
					"{} hull {:08X} xy={:.1f}->{:.1f} h={:.1f}->{:.1f}",
					a_kind == LowHull::Slide ? "slide/roll" : "combat crouch",
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
			Longsword,
			Warhammer,
			Battleaxe
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
			case WeaponPreset::Warhammer:
				return "warhammer";
			case WeaponPreset::Battleaxe:
				return "battleaxe";
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
			case WeaponPreset::Warhammer:
				return Settings::fWarhammer;
			case WeaponPreset::Battleaxe:
				return Settings::fBattleaxe;
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
			if (HasWeapKeyword(weap, "WeapTypeWarhammer")) {
				return WeaponPreset::Warhammer;
			}
			if (HasWeapKeyword(weap, "WeapTypeBattleaxe") || weap->IsTwoHandedAxe()) {
				return WeaponPreset::Battleaxe;
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
			if (a_actor->IsPlayerRef() && PlayerMountBlocksCombat(a_actor)) {
				return ScaleMath::kVanilla;
			}
			if (!a_player || a_actor == a_player || a_actor->IsPlayerRef()) {
				return a_combatScale;
			}

			if (Settings::bAllyCombatCollision && a_actor->IsPlayerTeammate() && HasCombatTarget(a_actor)) {
				return ScaleForPlayerWeapon(a_actor);
			}

			if (Settings::bAllyCombatCollision && HasCombatTarget(a_actor)) {
				if (const auto ally = a_actor->GetActorRuntimeData().currentCombatTarget.get()) {
					if (ally.get() != a_player && ally->IsPlayerTeammate()) {
						return ScaleForPlayerWeapon(ally.get());
					}
				}
			}

			return a_combatScale;
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
			RE::hkpListShape* list = nullptr;
			RE::hkpConvexVerticesShape* convexLive = nullptr;
			FindConvexList(a_controller, list, convexLive);

			float liveRadius = a_liveRadius;
			if (!ScaleMath::IsFinitePositive(liveRadius) && convexLive) {
				liveRadius = MeasureConvexXYWorld(convexLive);
			}

			const float baseR = VcdBaseRadius(formID, a_tracked.vanillaRadius);
			const bool skipUniformScale = ScaleMath::NeedsApply(a_target, ScaleMath::kVanilla) &&
				!VanillaHullScalable(baseR) && !VcdPresetScalable(liveRadius);
			if (skipUniformScale) {
				static RE::FormID lastSizeSkip = 0;
				if (formID != lastSizeSkip) {
					lastSizeSkip = formID;
					logger::debug(
						"skip uniform scale {:08X} vanillaR={:.1f} liveR={:.1f}",
						formID,
						baseR,
						liveRadius);
				}
			}

			float slider = a_target;
			const bool snapGrowNow = a_snapGrow;
			float combatApplied = CombatAppliedFromSlider(a_target, baseR);

			if (ScaleMath::NeedsApply(a_target, ScaleMath::kVanilla)) {
				MaybeSnapshotCombatHull(
					a_tracked, list, convexLive, a_target, liveRadius,
					ScaleMath::FightOverrideWantedRadius(
						liveRadius, baseR, a_target, kHumanXYRadius));
			}

			if (!ScaleMath::IsFinitePositive(liveRadius)) {
				return false;
			}

			if (skipUniformScale) {
				a_tracked.applied = combatApplied;
				return true;
			}

			const float wantCombatR = ScaleMath::FightOverrideWantedRadius(
				liveRadius, baseR, slider, kHumanXYRadius);
			combatApplied = CombatAppliedFromSlider(slider, baseR);

			if (!ScaleMath::RadiusNeedsScale(liveRadius, wantCombatR)) {
				MaybeSnapshotCombatHull(
					a_tracked, list, convexLive, a_target, liveRadius, wantCombatR);
			}

			const float wantR = wantCombatR;
			if (!ScaleMath::RadiusNeedsScale(liveRadius, wantR)) {
				a_tracked.applied = combatApplied;
				MaybeSnapshotCombatHull(
					a_tracked, list, convexLive, a_target, liveRadius, wantCombatR);
				return true;
			}

			float factor = ScaleMath::RadiusScaleFactor(liveRadius, wantR);
			if (wantR > liveRadius && a_actor->IsPlayerRef() && !snapGrowNow && !g_vcdFightOverride) {
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
				a_tracked, list, convexLive, a_target, liveRadius, wantCombatR);
			if (previous <= ScaleMath::kVanilla + ScaleMath::kEpsilon ||
				slider <= ScaleMath::kVanilla + ScaleMath::kEpsilon ||
				snapGrowNow) {
				logger::debug(
					"collision xy {:08X} live={:.1f} want={:.1f} base={:.1f}",
					formID,
					liveRadius,
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

			if (a_actor->IsPlayerRef()) {
				const auto low = DesiredLowHull(a_actor);
				if (tracked.slideActive &&
					(low == LowHull::None ||
						(tracked.poseShrink == LowHull::Slide && low == LowHull::Crouch))) {
					RestoreSlideHull(a_actor, controller, tracked);
					if (low == LowHull::None && !PlayerCombatHullMode(a_actor)) {
						return true;
					}
				}

				const auto finishPose = [&](bool a_ok) {
					if (!a_ok || low == LowHull::None) {
						return a_ok;
					}
					return ApplyLowHullShrink(a_actor, controller, tracked, low);
				};

				if (low == LowHull::Slide) {
					return ApplyLowHullShrink(a_actor, controller, tracked, low);
				}

				if (g_vcdFightOverride) {
					return finishPose(
						ApplyVcdFightScale(a_actor, a_target, controller, xyRadius, tracked, snapGrow));
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
					return finishPose(true);
				}

				if (!ScaleMath::IsFinitePositive(tracked.applied) || !std::isfinite(targetApplied) ||
					targetApplied < ScaleMath::kEpsilon) {
					return false;
				}

				float factor = targetApplied / tracked.applied;
				if (targetApplied > tracked.applied && a_actor->IsPlayerRef() && !snapGrow &&
					!g_vcdFightOverride) {
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
				return finishPose(true);
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
			if (targetApplied > tracked.applied && a_actor->IsPlayerRef() && !snapGrow &&
				!g_vcdFightOverride) {
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
			if (auto* player = RE::PlayerCharacter::GetSingleton()) {
				if (player->GetFormID() == a_formID && PlayerCombatHullMode(player)) {
					return;
				}
			}

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

		float MeasureLiveConvexXYWorld(RE::hkpConvexVerticesShape* a_convex)
		{
			if (!a_convex) {
				return 0.0f;
			}

			std::vector<RE::hkVector4> live;
			CopyLiveConvexVertices(a_convex, live);
			const float inv = RE::bhkWorld::GetWorldScaleInverse();
			float maxR = 0.0f;
			if (!live.empty()) {
				for (const auto& vert : live) {
					const float x = vert.quad.m128_f32[0];
					const float y = vert.quad.m128_f32[1];
					const float r = std::sqrt(x * x + y * y) * inv;
					if (r > maxR) {
						maxR = r;
					}
				}
				return maxR;
			}
			return MeasureConvexXYWorld(a_convex);
		}

		float LiveHullRadiusHk(RE::Actor* a_actor, const Tracked* a_tracked)
		{
			if (a_actor) {
				auto* controller = a_actor->GetCharController();
				auto* cell = a_actor->GetParentCell();
				auto* world = cell ? cell->GetbhkWorld() : nullptr;
				if (controller && world) {
					RE::hkpListShape* list = nullptr;
					RE::hkpConvexVerticesShape* convex = nullptr;
					float worldR = 0.0f;
					{
						RE::BSReadLockGuard lock(world->worldLock);
						if (FindConvexList(controller, list, convex)) {
							worldR = MeasureLiveConvexXYWorld(convex);
						}
					}
					if (ScaleMath::IsFinitePositive(worldR)) {
						return worldR * RE::bhkWorld::GetWorldScale();
					}
				}
			}
			return ActorRadiusHk(a_tracked);
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
			auto* body = CharacterHullObject(controller);
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
					logger::info(
						"debug hull combat={} xyRadius={:.1f} applied={:.2f} verts={} enabled={}",
						a_actor->IsInCombat(),
						maxR,
						applied,
						verts.size(),
						Settings::bEnabled);
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
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (player) {
				DrawActorHull(hud, player, true);
				drawn.insert(player->GetFormID());
			}

			std::uint32_t extra = 0;
			const auto drawExtra = [&](RE::Actor* a_actor) {
				if (!a_actor || extra >= 7) {
					return;
				}
				const auto id = a_actor->GetFormID();
				if (drawn.contains(id)) {
					return;
				}
				DrawActorHull(hud, a_actor, false);
				drawn.insert(id);
				++extra;
			};

			for (const auto& [id, tracked] : g_tracked) {
				if (const auto actor = tracked.handle.get()) {
					drawExtra(actor.get());
				}
			}
			if (player && (player->IsInCombat() || PlayerCombatHullMode(player))) {
				ForEachFightNpc(player, [&](RE::Actor* a_npc) {
					drawExtra(a_npc);
				});
			}
			if (extra < 7) {
				constexpr std::uint32_t kGreen = 0x00FF00FF;
				for (std::uint32_t i = 0; i < kMaxActors && extra < 7; ++i) {
					const auto& slot = g_thunkHulls[i];
					if (!slot.proxy || slot.isPlayer || slot.liveR <= 1.0f) {
						continue;
					}
					hud->DrawCircle(
						RE::NiPoint3{ slot.worldX, slot.worldY, slot.worldZ },
						RE::NiPoint3{ 1.0f, 0.0f, 0.0f },
						RE::NiPoint3{ 0.0f, 1.0f, 0.0f },
						slot.liveR,
						24,
						0.0f,
						kGreen,
						2.0f);
					++extra;
				}
			}
		}

		Tracked* FindTrackedByController(RE::bhkCharacterController* a_controller)
		{
			if (!a_controller) {
				return nullptr;
			}
			auto* proxy = AsCharProxyController(a_controller);
			RE::bhkCharacterController* cc = nullptr;
			if (proxy) {
				constexpr std::uintptr_t kListenerToCC = 0x10;
				cc = reinterpret_cast<RE::bhkCharacterController*>(
					reinterpret_cast<std::uintptr_t>(proxy) + kListenerToCC);
			}
			for (auto& [id, tracked] : g_tracked) {
				if (tracked.controller == a_controller ||
					(cc && tracked.controller == cc) ||
					(proxy && tracked.controller == static_cast<RE::bhkCharacterController*>(proxy))) {
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
			if (!Settings::bEnabled) {
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
			if (tracked && tracked->slideActive) {
				return;
			}

			// Prefer the hull scaled on this physics call. NPC g_tracked.applied often stays
			// vanilla after the thunk already grew the convex, which used to skip this filter
			// and leave fat NPC hulls stuck on doorframes. Player tracking stays in sync.
			float applied = g_thunkAppliedThisCall;
			if (tracked && ScaleMath::NeedsApply(tracked->applied, ScaleMath::kVanilla) &&
				tracked->applied > applied) {
				applied = tracked->applied;
			}
			if (!ScaleMath::NeedsApply(applied, ScaleMath::kVanilla)) {
				RE::hkpListShape* list = nullptr;
				RE::hkpConvexVerticesShape* convex = nullptr;
				if (FindConvex(a_proxy->shapePhantom, list, convex)) {
					const float live = MeasureConvexXYWorld(convex);
					if (live > kMaxVanillaXY) {
						applied = live / kHumanXYRadius;
					}
				}
			}
			if (!ScaleMath::NeedsApply(applied, ScaleMath::kVanilla)) {
				return;
			}

			static bool loggedNpcWorld = false;
			if (!loggedNpcWorld) {
				const auto actor = tracked ? tracked->handle.get() : RE::NiPointer<RE::Actor>{};
				if (!actor || !actor->IsPlayerRef()) {
					loggedNpcWorld = true;
					logger::info(
						"world filter npc applied={:.2f} thunk={:.2f} tracked={:.2f}",
						applied,
						g_thunkAppliedThisCall,
						tracked ? tracked->applied : 0.0f);
				}
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

			const float combatHk = vanillaHk * (applied > 1.0f ? applied : 1.0f);
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

		void FilterPlayerActorSolverConstraints(
			RE::bhkCharProxyController* a_self,
			const RE::hkpCharacterProxy* a_proxy,
			const RE::hkArray<RE::hkpRootCdPoint>& a_manifold,
			RE::hkpSimplexSolverInput& a_input)
		{
			if (!Settings::bEnabled || !Settings::bPlayerImmovable) {
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

			const auto actor = tracked->handle.get();
			if (!actor || !actor->IsPlayerRef() || PlayerMountBlocksCombat(actor.get())) {
				return;
			}

			const auto* selfCol = static_cast<const RE::hkpCollidable*>(&a_proxy->shapePhantom->collidable);
			const float vx = a_input.velocity.quad.m128_f32[0];
			const float vy = a_input.velocity.quad.m128_f32[1];
			const auto manifoldCount = a_manifold.size();
			const auto limit = manifoldCount < a_input.numConstraints ?
				manifoldCount :
				a_input.numConstraints;

			for (RE::hkArray<RE::hkpRootCdPoint>::size_type i = 0; i < limit; ++i) {
				const auto& pt = a_manifold[i];
				const RE::hkpCollidable* other = ManifoldOtherCollidable(pt, selfCol);
				if (!other || !IsActorCollisionLayer(other->GetCollisionLayer())) {
					continue;
				}

				const float nx = pt.contact.separatingNormal.quad.m128_f32[0];
				const float ny = pt.contact.separatingNormal.quad.m128_f32[1];
				if (nx * nx + ny * ny < 0.35f * 0.35f) {
					continue;
				}
				if (WallClip::KeepPlayerActorConstraint(vx, vy, nx, ny)) {
					continue;
				}

				a_input.constraints[i].plane.quad.m128_f32[3] = WallClip::kSatisfiedSimplexPlaneW;
			}
		}

		bool IsRangedCombatant(RE::Actor* a_actor);
		bool IsMeleeAttacking(RE::Actor* a_actor);
		bool IsMeleeAttackLaunch(RE::Actor* a_actor);

		void ReleaseNpcPlayerGapConstraints(
			RE::bhkCharProxyController* a_self,
			const RE::hkpCharacterProxy* a_proxy,
			const RE::hkArray<RE::hkpRootCdPoint>& a_manifold,
			RE::hkpSimplexSolverInput& a_input)
		{
			if (!Settings::bEnabled || !Settings::bTranslationHelper) {
				return;
			}
			if (PlayerMountBlocksCombat(RE::PlayerCharacter::GetSingleton())) {
				return;
			}
			if (!a_self || !a_proxy || !a_proxy->shapePhantom || !a_input.constraints) {
				return;
			}
			if (a_input.numConstraints <= 0 || a_manifold.empty()) {
				return;
			}

			auto* playerProxy = PlayerCharacterProxy();
			if (!playerProxy || !playerProxy->shapePhantom || a_proxy == playerProxy) {
				return;
			}

			auto* controller = static_cast<RE::bhkCharacterController*>(a_self);
			auto* tracked = FindTrackedByController(controller);
			if (tracked && tracked->slideActive) {
				return;
			}

			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* playerTracked = player ?
				FindTrackedByController(player->GetCharController()) :
				nullptr;
			const auto npc = tracked ? tracked->handle.get() : RE::NiPointer<RE::Actor>{};
			if (!IsMeleeAttacking(npc.get())) {
				return;
			}
			const auto& playerT = playerProxy->shapePhantom->motionState.transform.translation;
			const auto& npcT = a_proxy->shapePhantom->motionState.transform.translation;
			const float dx = npcT.quad.m128_f32[0] - playerT.quad.m128_f32[0];
			const float dy = npcT.quad.m128_f32[1] - playerT.quad.m128_f32[1];
			const float dist2 = dx * dx + dy * dy;
			if (!std::isfinite(dist2) || dist2 < 1.0e-8f) {
				return;
			}
			const float dist = std::sqrt(dist2);
			const float combined =
				LiveHullRadiusHk(player, playerTracked) + LiveHullRadiusHk(npc.get(), tracked);
			if (dist < combined) {
				return;
			}

			const auto* selfCol = static_cast<const RE::hkpCollidable*>(&a_proxy->shapePhantom->collidable);
			const auto manifoldCount = a_manifold.size();
			const auto limit = manifoldCount < a_input.numConstraints ?
				manifoldCount :
				a_input.numConstraints;
			for (RE::hkArray<RE::hkpRootCdPoint>::size_type i = 0; i < limit; ++i) {
				const auto& pt = a_manifold[i];
				const RE::hkpCollidable* other = ManifoldOtherCollidable(pt, selfCol);
				if (!other || !IsActorCollisionLayer(other->GetCollisionLayer())) {
					continue;
				}
				if (!IsPlayerCollidable(other)) {
					continue;
				}
				a_input.constraints[i].plane.quad.m128_f32[3] = WallClip::kSatisfiedSimplexPlaneW;
			}
		}

		void BlockNpcIntoPlayerHull(
			RE::bhkCharProxyController* a_self,
			const RE::hkpCharacterProxy* a_proxy,
			const RE::hkArray<RE::hkpRootCdPoint>& a_manifold,
			RE::hkpSimplexSolverInput& a_input)
		{
			if (!Settings::bEnabled || !Settings::bPlayerImmovable) {
				return;
			}
			if (PlayerMountBlocksCombat(RE::PlayerCharacter::GetSingleton())) {
				return;
			}
			if (!a_self || !a_proxy || !a_proxy->shapePhantom) {
				return;
			}

			auto* playerProxy = PlayerCharacterProxy();
			if (!playerProxy || !playerProxy->shapePhantom || a_proxy == playerProxy) {
				return;
			}

			auto* controller = static_cast<RE::bhkCharacterController*>(a_self);
			auto* tracked = FindTrackedByController(controller);
			if (tracked && tracked->slideActive) {
				return;
			}

			static bool loggedNpcTick = false;
			if (!loggedNpcTick) {
				loggedNpcTick = true;
				logger::info("Don't get pushed: NPC movement tick is running");
			}

			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* playerTracked = player ?
				FindTrackedByController(player->GetCharController()) :
				nullptr;

			const auto& playerT = playerProxy->shapePhantom->motionState.transform.translation;
			const auto& npcT = a_proxy->shapePhantom->motionState.transform.translation;
			const float dx = npcT.quad.m128_f32[0] - playerT.quad.m128_f32[0];
			const float dy = npcT.quad.m128_f32[1] - playerT.quad.m128_f32[1];
			const float dist2 = dx * dx + dy * dy;
			if (dist2 < 1.0e-8f) {
				return;
			}
			const float dist = std::sqrt(dist2);
			const auto npc = tracked ? tracked->handle.get() : RE::NiPointer<RE::Actor>{};
			const float combined =
				LiveHullRadiusHk(player, playerTracked) + LiveHullRadiusHk(npc.get(), tracked);
			if (dist >= combined) {
				return;
			}

			const float inv = 1.0f / dist;
			const float nxx = dx * inv;
			const float nyy = dy * inv;
			float& vx = a_input.velocity.quad.m128_f32[0];
			float& vy = a_input.velocity.quad.m128_f32[1];
			const float vn = vx * nxx + vy * nyy;
			const float recover = a_proxy->penetrationRecoverySpeed > 1.0f ?
				a_proxy->penetrationRecoverySpeed :
				1.0f;
			const float wantVn = (combined - dist) * recover;
			if (vn < wantVn) {
				const float add = wantVn - vn;
				vx += nxx * add;
				vy += nyy * add;
				static bool loggedBlock = false;
				if (!loggedBlock) {
					loggedBlock = true;
					logger::info(
						"Don't get pushed: blocked NPC walking into player hull (distHk={:.3f} combinedHk={:.3f})",
						dist,
						combined);
				}
			}

			if (!a_input.constraints || a_input.numConstraints <= 0 || a_manifold.empty()) {
				return;
			}

			const auto* selfCol = static_cast<const RE::hkpCollidable*>(&a_proxy->shapePhantom->collidable);
			const auto manifoldCount = a_manifold.size();
			const auto limit = manifoldCount < a_input.numConstraints ?
				manifoldCount :
				a_input.numConstraints;
			for (RE::hkArray<RE::hkpRootCdPoint>::size_type i = 0; i < limit; ++i) {
				const auto& pt = a_manifold[i];
				const RE::hkpCollidable* other = ManifoldOtherCollidable(pt, selfCol);
				if (!other || !IsActorCollisionLayer(other->GetCollisionLayer())) {
					continue;
				}
				if (!IsPlayerCollidable(other) && dist >= combined * 0.98f) {
					continue;
				}

				auto& constraint = a_input.constraints[i];
				constraint.plane.quad.m128_f32[0] = nxx;
				constraint.plane.quad.m128_f32[1] = nyy;
				constraint.plane.quad.m128_f32[2] = 0.0f;
				constraint.plane.quad.m128_f32[3] =
					WallClip::VanillaWorldStopDistanceHk(dist, combined, 0.0f);
				constraint.velocity.quad.m128_f32[0] = 0.0f;
				constraint.velocity.quad.m128_f32[1] = 0.0f;
				constraint.velocity.quad.m128_f32[2] = 0.0f;
				constraint.velocity.quad.m128_f32[3] = 0.0f;
			}
		}

		RE::Actor* ActorFromCharController(RE::bhkCharacterController* a_controller)
		{
			if (!a_controller) {
				return nullptr;
			}

			auto* player = RE::PlayerCharacter::GetSingleton();
			if (player && player->GetCharController() == a_controller) {
				return player;
			}

			if (auto* tracked = FindTrackedByController(a_controller)) {
				if (const auto actor = tracked->handle.get()) {
					return actor.get();
				}
			}

			const auto* lists = RE::ProcessLists::GetSingleton();
			if (!lists) {
				return nullptr;
			}
			for (auto& handle : lists->highActorHandles) {
				const auto actor = handle.get();
				if (actor && actor->GetCharController() == a_controller) {
					return actor.get();
				}
			}
			return nullptr;
		}

		void RebuildPhantomCache()
		{
			PhantomScale tmp[kMaxActors]{};
			std::uint32_t n = 0;
			const auto add = [&](RE::Actor* a_actor, float a_slider) {
				if (!a_actor || n >= kMaxActors) {
					return;
				}
				if (!ScaleMath::NeedsApply(a_slider, ScaleMath::kVanilla)) {
					return;
				}
				auto* proxyCtrl = AsCharProxyController(a_actor->GetCharController());
				auto* proxy = proxyCtrl ? proxyCtrl->GetCharacterProxy() : nullptr;
				if (!proxy) {
					return;
				}
				tmp[n].proxy = proxy;
				tmp[n].slider = a_slider;
				++n;
			};

			auto* player = RE::PlayerCharacter::GetSingleton();
			const float playerSlider = ScaleForPlayerWeapon(player);
			if (player && PlayerCombatHullMode(player)) {
				add(player, playerSlider);
			}
			if (player) {
				ForEachFightNpc(player, [&](RE::Actor* a_npc) {
					add(a_npc, ScaleForActor(a_npc, player, playerSlider));
				});
			}
			for (std::uint32_t i = 0; i < n; ++i) {
				g_phantomScales[i] = tmp[i];
			}
			g_phantomCount = n;
		}

		ThunkHull* GetThunkHull(const RE::hkpCharacterProxy* a_proxy)
		{
			if (!a_proxy) {
				return nullptr;
			}
			std::int32_t empty = -1;
			for (std::uint32_t i = 0; i < kMaxActors; ++i) {
				if (g_thunkHulls[i].proxy == a_proxy) {
					return &g_thunkHulls[i];
				}
				if (empty < 0 && !g_thunkHulls[i].proxy) {
					empty = static_cast<std::int32_t>(i);
				}
			}
			const auto idx = empty >= 0 ? static_cast<std::uint32_t>(empty) : 0u;
			g_thunkHulls[idx] = ThunkHull{};
			g_thunkHulls[idx].proxy = a_proxy;
			g_thunkHulls[idx].applied = ScaleMath::kVanilla;
			return &g_thunkHulls[idx];
		}

		void ApplyCombatHullOnPhantom(
			RE::bhkCharProxyController* a_self,
			const RE::hkpCharacterProxy* a_proxy)
		{
			g_thunkAppliedThisCall = ScaleMath::kVanilla;
			if (!Settings::bEnabled || !a_self || !a_proxy || !a_proxy->shapePhantom) {
				return;
			}

			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) {
				return;
			}

			RE::hkpListShape* list = nullptr;
			RE::hkpConvexVerticesShape* convex = nullptr;
			if (!FindConvex(a_proxy->shapePhantom, list, convex)) {
				return;
			}

			const float live = MeasureConvexXYWorld(convex);
			auto* slot = GetThunkHull(a_proxy);
			if (!slot) {
				return;
			}
			if (!ScaleMath::IsFinitePositive(slot->vanillaR)) {
				slot->vanillaR = live;
			}

			const auto* playerProxy = PlayerCharacterProxy();
			const bool isPlayer = a_proxy == playerProxy ||
				(playerProxy && a_proxy->shapePhantom == playerProxy->shapePhantom);
			slot->isPlayer = isPlayer;

			float slider = ScaleMath::kVanilla;
			if (PlayerCombatHullMode(player)) {
				if (isPlayer) {
					slider = ScaleForPlayerWeapon(player);
				} else {
					const auto* selfCol =
						static_cast<const RE::hkpCollidable*>(&a_proxy->shapePhantom->collidable);
					auto* refr = RE::TESHavokUtilities::FindCollidableRef(*selfCol);
					auto* actor = refr ? refr->As<RE::Actor>() : nullptr;
					const bool allowed = actor ?
						ShouldScaleHull(actor) :
						VanillaHullScalable(slot->vanillaR);
					const bool nearby = PhantomsWithinFightRange(a_proxy, playerProxy);
					const bool inFight = actor ?
						(InPlayersFight(actor, player) || nearby) :
						nearby;
					if (allowed && inFight) {
						slider = actor ?
							ScaleForActor(actor, player, ScaleForPlayerWeapon(player)) :
							ScaleForPlayerWeapon(player);
					}
				}
			}

			const auto n = g_phantomCount;
			for (std::uint32_t i = 0; i < n && i < kMaxActors; ++i) {
				if (g_phantomScales[i].proxy == a_proxy) {
					slider = g_phantomScales[i].slider;
					break;
				}
			}

			const float want = ScaleMath::FightOverrideWantedRadius(
				live, slot->vanillaR, slider, kHumanXYRadius);
			if (ScaleMath::RadiusNeedsScale(live, want)) {
				const float factor = ScaleMath::RadiusScaleFactor(live, want);
				if (factor != 0.0f) {
					ScaleConvexXYInPlace(list, convex, factor);
				}
			}

			slot->applied = CombatAppliedFromSlider(slider, slot->vanillaR);
			g_thunkAppliedThisCall = slot->applied;
			const float inv = RE::bhkWorld::GetWorldScaleInverse();
			const auto& translation = a_proxy->shapePhantom->motionState.transform.translation;
			slot->worldX = translation.quad.m128_f32[0] * inv;
			slot->worldY = translation.quad.m128_f32[1] * inv;
			slot->worldZ = translation.quad.m128_f32[2] * inv;
			slot->liveR = MeasureConvexXYWorld(convex);

			static bool loggedPlayer = false;
			static bool loggedNpc = false;
			if (isPlayer && !loggedPlayer && ScaleMath::NeedsApply(slider, ScaleMath::kVanilla)) {
				loggedPlayer = true;
				logger::info(
					"combat hull phantom player xy {:.1f}->{:.1f} slider={:.2f}",
					live,
					want,
					slider);
			} else if (!isPlayer && !loggedNpc && ScaleMath::NeedsApply(slider, ScaleMath::kVanilla)) {
				loggedNpc = true;
				logger::info(
					"combat hull phantom npc xy {:.1f}->{:.1f} slider={:.2f} vanilla={:.1f}",
					live,
					want,
					slider,
					slot->vanillaR);
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
			ApplyCombatHullOnPhantom(a_self, a_proxy);
			_ProcessConstraints(a_self, a_proxy, a_manifold, a_input);
			FilterWorldSolverConstraints(a_self, a_proxy, a_manifold, a_input);
			FilterPlayerActorSolverConstraints(a_self, a_proxy, a_manifold, a_input);
			ReleaseNpcPlayerGapConstraints(a_self, a_proxy, a_manifold, a_input);
			BlockNpcIntoPlayerHull(a_self, a_proxy, a_manifold, a_input);
		}

		REL::Relocation<void(RE::bhkCharProxyController*, RE::hkpCharacterProxy*, RE::hkpCharacterProxy*, const RE::hkContactPoint&)>
			_CharacterInteraction;

		void CharacterInteractionThunk(
			RE::bhkCharProxyController* a_self,
			RE::hkpCharacterProxy* a_proxy,
			RE::hkpCharacterProxy* a_otherProxy,
			const RE::hkContactPoint& a_contact)
		{
			if (!Settings::bEnabled || !a_proxy || !a_otherProxy) {
				_CharacterInteraction(a_self, a_proxy, a_otherProxy, a_contact);
				return;
			}
			if (PlayerMountBlocksCombat(RE::PlayerCharacter::GetSingleton())) {
				_CharacterInteraction(a_self, a_proxy, a_otherProxy, a_contact);
				return;
			}
			if (!Settings::bPlayerImmovable && !Settings::bTranslationHelper) {
				_CharacterInteraction(a_self, a_proxy, a_otherProxy, a_contact);
				return;
			}

			auto* playerProxy = PlayerCharacterProxy();
			if (!playerProxy || (a_proxy != playerProxy && a_otherProxy != playerProxy)) {
				_CharacterInteraction(a_self, a_proxy, a_otherProxy, a_contact);
				return;
			}

			if (a_proxy == playerProxy) {
				return;
			}

			auto* tracked = FindTrackedByController(static_cast<RE::bhkCharacterController*>(a_self));
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* playerTracked = player ?
				FindTrackedByController(player->GetCharController()) :
				nullptr;
			if (playerProxy->shapePhantom && a_proxy->shapePhantom) {
				const auto& playerT = playerProxy->shapePhantom->motionState.transform.translation;
				const auto& npcT = a_proxy->shapePhantom->motionState.transform.translation;
				const float dx = npcT.quad.m128_f32[0] - playerT.quad.m128_f32[0];
				const float dy = npcT.quad.m128_f32[1] - playerT.quad.m128_f32[1];
				const auto npc = tracked ? tracked->handle.get() : RE::NiPointer<RE::Actor>{};
				const float dist2 = dx * dx + dy * dy;
				if (std::isfinite(dist2) && dist2 >= 1.0e-8f) {
					const float dist = std::sqrt(dist2);
					const float combined =
						LiveHullRadiusHk(player, playerTracked) + LiveHullRadiusHk(npc.get(), tracked);
					if (dist >= combined) {
						if (Settings::bPlayerImmovable ||
							(Settings::bTranslationHelper && IsMeleeAttacking(npc.get()))) {
							return;
						}
						_CharacterInteraction(a_self, a_proxy, a_otherProxy, a_contact);
						return;
					}
				}
			}

			const float nx = a_contact.separatingNormal.quad.m128_f32[0];
			const float ny = a_contact.separatingNormal.quad.m128_f32[1];
			const float nxy2 = nx * nx + ny * ny;
			if (nxy2 < 0.35f * 0.35f) {
				return;
			}
			const float inv = 1.0f / std::sqrt(nxy2);
			const float nxx = nx * inv;
			const float nyy = ny * inv;
			float& vx = a_proxy->velocity.quad.m128_f32[0];
			float& vy = a_proxy->velocity.quad.m128_f32[1];
			const float vn = vx * nxx + vy * nyy;
			const float dist = a_contact.separatingNormal.quad.m128_f32[3];
			const float keep = a_proxy->keepDistance;
			float wantVn = 0.0f;
			if (dist < keep) {
				const float recover = a_proxy->penetrationRecoverySpeed > 1.0f ?
					a_proxy->penetrationRecoverySpeed :
					1.0f;
				wantVn = (keep - dist) * recover;
			}
			if (vn < wantVn) {
				const float add = wantVn - vn;
				vx += nxx * add;
				vy += nyy * add;
			}

			static bool logged = false;
			if (!logged) {
				logged = true;
				logger::info("Don't get pushed: NPC bump is a one-way wall (player does not move)");
			}
		}

		bool IsRangedCombatant(RE::Actor* a_actor)
		{
			if (!a_actor) {
				return false;
			}
			auto* obj = a_actor->GetEquippedObject(false);
			auto* weap = obj ? obj->As<RE::TESObjectWEAP>() : nullptr;
			return weap && (weap->IsBow() || weap->IsCrossbow() || weap->IsStaff());
		}

		bool IsMeleeAttacking(RE::Actor* a_actor)
		{
			if (!a_actor || IsRangedCombatant(a_actor)) {
				return false;
			}
			if (a_actor->IsAttacking()) {
				return true;
			}
			auto* state = a_actor->AsActorState();
			if (!state) {
				return false;
			}
			const auto atk = state->GetAttackState();
			return atk != RE::ATTACK_STATE_ENUM::kNone &&
				atk < RE::ATTACK_STATE_ENUM::kBowDraw;
		}

		bool IsMeleeAttackLaunch(RE::Actor* a_actor)
		{
			if (!a_actor || IsRangedCombatant(a_actor)) {
				return false;
			}
			auto* state = a_actor->AsActorState();
			if (!state) {
				return false;
			}
			const auto atk = state->GetAttackState();
			using Attack = RE::ATTACK_STATE_ENUM;
			return atk == Attack::kDraw ||
				atk == Attack::kSwing ||
				atk == Attack::kNextAttack ||
				atk == Attack::kBash;
		}

		void CloseNpcsToCombatHull(float a_delta)
		{
			if (!Settings::bEnabled) {
				return;
			}

			++g_hullCloseFrame;

			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player || !player->IsInCombat() || PlayerMountBlocksCombat(player)) {
				return;
			}
			auto* playerCtrl = player->GetCharController();
			if (!playerCtrl) {
				return;
			}
			auto* playerTracked = FindTrackedByController(playerCtrl);
			if (playerTracked && playerTracked->slideActive) {
				return;
			}
			if (!playerTracked || !ScaleMath::NeedsApply(playerTracked->applied, ScaleMath::kVanilla)) {
				return;
			}

			const float dt = (std::isfinite(a_delta) && a_delta > 1.0e-4f && a_delta < 0.1f) ?
				a_delta :
				1.0f / 60.0f;
			const float worldScale = RE::bhkWorld::GetWorldScale();
			const float playerR = LiveHullRadiusHk(player, playerTracked);
			const float epsHk = kHullCloseEpsWorld * worldScale;
			const float maxGapHk = kHullCloseGapWorld * worldScale;
			const float stepHk = kHullCloseSpeedWorld * worldScale * dt;
			const auto* lists = RE::ProcessLists::GetSingleton();
			if (!lists) {
				return;
			}

			RE::hkVector4 playerPos;
			playerCtrl->GetPosition(playerPos, false);

			for (auto& handle : lists->highActorHandles) {
				const auto actor = handle.get();
				if (!actor || actor->IsPlayerRef() || ActorIsGone(actor.get()) || !actor->Is3DLoaded()) {
					continue;
				}
				if (actor->IsPlayerTeammate()) {
					continue;
				}
				if (!InPlayersFight(actor.get(), player)) {
					continue;
				}
				if (IsRangedCombatant(actor.get()) || !IsMeleeAttackLaunch(actor.get())) {
					continue;
				}
				auto* npcCtrl = actor->GetCharController();
				if (!npcCtrl || npcCtrl == playerCtrl) {
					continue;
				}

				RE::hkVector4 npcPos;
				npcCtrl->GetPosition(npcPos, false);
				const float dx = npcPos.quad.m128_f32[0] - playerPos.quad.m128_f32[0];
				const float dy = npcPos.quad.m128_f32[1] - playerPos.quad.m128_f32[1];
				const float dist2 = dx * dx + dy * dy;
				if (dist2 < 1.0e-8f) {
					continue;
				}
				const float dist = std::sqrt(dist2);
				auto* npcTracked = FindTrackedByController(npcCtrl);
				const float combined = playerR + LiveHullRadiusHk(actor.get(), npcTracked);
				const float extra = dist - combined;
				if (extra <= epsHk || extra > maxGapHk) {
					continue;
				}

				const float inv = 1.0f / dist;
				const float nxx = dx * inv;
				const float nyy = dy * inv;

				const auto rememberClose = [&]() {
					if (!npcTracked) {
						return;
					}
					npcTracked->closeFrame = g_hullCloseFrame;
					npcTracked->closeLastX = npcPos.quad.m128_f32[0];
					npcTracked->closeLastY = npcPos.quad.m128_f32[1];
				};

				RE::hkVector4 vel;
				npcCtrl->GetLinearVelocityImpl(vel);
				const float wantHk = kHullCloseSpeedWorld * worldScale;
				bool alreadyStepping = Settings::bTranslationHelper && actor->IsAnimationDriven();
				if (!alreadyStepping && npcTracked && npcTracked->closeFrame + 1 == g_hullCloseFrame) {
					const float natDx = npcPos.quad.m128_f32[0] - npcTracked->closeLastX;
					const float natDy = npcPos.quad.m128_f32[1] - npcTracked->closeLastY;
					const float inbound = -(natDx * nxx + natDy * nyy);
					alreadyStepping = inbound > stepHk * kHullCloseStepSkip;
				}
				if (alreadyStepping) {
					rememberClose();
					continue;
				}

				const float move = extra - epsHk < stepHk ? extra - epsHk : stepHk;

				npcPos.quad.m128_f32[0] -= nxx * move;
				npcPos.quad.m128_f32[1] -= nyy * move;
				npcCtrl->SetPositionImpl(npcPos, false, false);

				const float vn = vel.quad.m128_f32[0] * nxx + vel.quad.m128_f32[1] * nyy;
				if (vn > -wantHk * 0.5f) {
					vel.quad.m128_f32[0] -= (vn + wantHk) * nxx;
					vel.quad.m128_f32[1] -= (vn + wantHk) * nyy;
					npcCtrl->SetLinearVelocityImpl(vel);
				}
				rememberClose();

				static bool loggedClose = false;
				if (!loggedClose) {
					loggedClose = true;
					logger::info(
						"Combat hull close: moving NPC to hull contact (distHk={:.3f} combinedHk={:.3f} gapHk={:.3f})",
						dist,
						combined,
						extra);
				}
			}
		}

		void StopNpcsWalkingThroughPlayer()
		{
			if (!Settings::bEnabled || !Settings::bPlayerImmovable) {
				return;
			}

			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player || PlayerMountBlocksCombat(player)) {
				return;
			}
			auto* playerCtrl = player->GetCharController();
			if (!playerCtrl) {
				return;
			}
			auto* playerTracked = FindTrackedByController(playerCtrl);
			if (playerTracked && playerTracked->slideActive) {
				return;
			}
			if (!playerTracked || !ScaleMath::NeedsApply(playerTracked->applied, ScaleMath::kVanilla)) {
				return;
			}

			static bool loggedTick = false;
			if (!loggedTick) {
				loggedTick = true;
				logger::info("Don't get pushed: player-update NPC block is running");
			}

			RE::hkVector4 playerPos;
			playerCtrl->GetPosition(playerPos, false);
			const float playerR = LiveHullRadiusHk(player, playerTracked);
			const auto* lists = RE::ProcessLists::GetSingleton();
			if (!lists) {
				return;
			}

			for (auto& handle : lists->highActorHandles) {
				const auto actor = handle.get();
				if (!actor || actor->IsPlayerRef() || ActorIsGone(actor.get()) || !actor->Is3DLoaded()) {
					continue;
				}
				auto* npcCtrl = actor->GetCharController();
				if (!npcCtrl || npcCtrl == playerCtrl) {
					continue;
				}

				RE::hkVector4 npcPos;
				npcCtrl->GetPosition(npcPos, false);
				const float dx = npcPos.quad.m128_f32[0] - playerPos.quad.m128_f32[0];
				const float dy = npcPos.quad.m128_f32[1] - playerPos.quad.m128_f32[1];
				const float dist2 = dx * dx + dy * dy;
				if (dist2 < 1.0e-8f) {
					continue;
				}
				const float dist = std::sqrt(dist2);
				auto* npcTracked = FindTrackedByController(npcCtrl);
				const float combined = playerR + LiveHullRadiusHk(actor.get(), npcTracked);
				if (dist >= combined) {
					continue;
				}

				const float inv = 1.0f / dist;
				const float nxx = dx * inv;
				const float nyy = dy * inv;

				RE::hkVector4 vel;
				npcCtrl->GetLinearVelocityImpl(vel);
				const float vn = vel.quad.m128_f32[0] * nxx + vel.quad.m128_f32[1] * nyy;
				if (vn < 0.0f) {
					vel.quad.m128_f32[0] -= vn * nxx;
					vel.quad.m128_f32[1] -= vn * nyy;
					npcCtrl->SetLinearVelocityImpl(vel);
				}

				auto& outVel = npcCtrl->outVelocity;
				const float outVn = outVel.quad.m128_f32[0] * nxx + outVel.quad.m128_f32[1] * nyy;
				if (outVn < 0.0f) {
					outVel.quad.m128_f32[0] -= outVn * nxx;
					outVel.quad.m128_f32[1] -= outVn * nyy;
				}

				const float pen = combined - dist;
				npcPos.quad.m128_f32[0] += nxx * pen;
				npcPos.quad.m128_f32[1] += nyy * pen;
				npcCtrl->SetPositionImpl(npcPos, false, false);

				static bool loggedBlock = false;
				if (!loggedBlock) {
					loggedBlock = true;
					logger::info(
						"Don't get pushed: pushed NPC out of player hull (distHk={:.3f} combinedHk={:.3f})",
						dist,
						combined);
				}
			}
		}

		void InstallProxyHooksInternal()
		{
			REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_bhkCharProxyController[0] };
			_ProcessConstraints = vtbl.write_vfunc(1, ProcessConstraintsThunk);
			_CharacterInteraction = vtbl.write_vfunc(4, CharacterInteractionThunk);
			logger::info("Installed world-contact, player-actor, and NPC-vs-player wall filters on bhkCharProxyController");
			logger::info("npc hull: Actor::IsDead() virtual ignored; using ActorState lifeState");
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
		g_playerMountHold = false;
		ClearSlideSession();
		ClearRollArmed();
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
			ClearRollArmed();
		}
	}

	void InstallProxyHooks()
	{
		InstallProxyHooksInternal();
	}

	void Update(float a_delta)
	{
		Settings::ReloadIfChanged();

		auto* ui = RE::UI::GetSingleton();
		if (ui && ui->GameIsPaused()) {
			return;
		}

		if (auto* player = RE::PlayerCharacter::GetSingleton()) {
			if (PlayerMountBlocksCombat(player)) {
				RestoreForm(player->GetFormID());
			}
		}

		CloseNpcsToCombatHull(a_delta);
		StopNpcsWalkingThroughPlayer();

		static std::uint32_t frames = 0;
		bool fastCombatTick = false;
		if (auto* player = RE::PlayerCharacter::GetSingleton()) {
			if (player->IsInCombat()) {
				fastCombatTick = true;
			} else if (Settings::bAllyCombatCollision) {
				if (const auto* lists = RE::ProcessLists::GetSingleton()) {
					for (auto& handle : lists->highActorHandles) {
						const auto actor = handle.get();
						if (actor && actor->IsPlayerTeammate() &&
							(HasCombatTarget(actor.get()) ||
								ActorsWithinFightRange(actor.get(), player))) {
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
		if (++frames >= interval) {
			frames = 0;

			std::unordered_set<RE::FormID> wanted;
			CollectWanted(wanted);

			if (auto* logPlayer = RE::PlayerCharacter::GetSingleton(); logPlayer && logPlayer->IsInCombat()) {
				static std::uint32_t wantedLog = 0;
				if (++wantedLog >= 60) {
					wantedLog = 0;
					std::uint32_t extra = 0;
					for (const auto id : wanted) {
						if (id != logPlayer->GetFormID()) {
							++extra;
						}
					}
					std::uint32_t high = 0;
					std::uint32_t fight = 0;
					std::uint32_t humanoid = 0;
					if (const auto* lists = RE::ProcessLists::GetSingleton()) {
						for (auto& handle : lists->highActorHandles) {
							const auto actor = handle.get();
							if (!actor || actor.get() == logPlayer) {
								continue;
							}
							++high;
							if (!ShouldScaleHull(actor.get())) {
								continue;
							}
							++humanoid;
							if (InPlayersFight(actor.get(), logPlayer)) {
								++fight;
							}
						}
					}
					logger::info(
						"combat wanted={} extraNpcs={} tracked={} nearbyHigh={} inFight={} humanoid={}",
						wanted.size(),
						extra,
						g_tracked.size(),
						high,
						fight,
						humanoid);
				}
			}

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
				if (!ActorHullReady(actor)) {
					RestoreForm(id);
					continue;
				}
				const float target = Settings::bEnabled ?
					ScaleForActor(actor, player, combatScale) :
					ScaleMath::kVanilla;
				SetActorScale(actor, target);
			}
		}

		if (auto* livePlayer = RE::PlayerCharacter::GetSingleton()) {
			if (Settings::bEnabled && PlayerCombatHullMode(livePlayer)) {
				const float playerSlider = ScaleForPlayerWeapon(livePlayer);
				SetActorScale(livePlayer, playerSlider);
				ForEachFightNpc(livePlayer, [&](RE::Actor* a_npc) {
					SetActorScale(a_npc, ScaleForActor(a_npc, livePlayer, playerSlider));
				});
			}
		}
		RebuildPhantomCache();
		if (auto* logPlayer = RE::PlayerCharacter::GetSingleton();
			logPlayer && PlayerCombatHullMode(logPlayer)) {
			static std::uint32_t cacheLog = 0;
			if (++cacheLog >= 60) {
				cacheLog = 0;
				std::uint32_t close = 0;
				std::uint32_t usable = 0;
				std::uint32_t humanoid = 0;
				std::uint32_t fight = 0;
				ForEachNearbyLoadedActor(logPlayer, [&](RE::Actor* actor) {
					++close;
					static std::uint32_t closeDump = 0;
					if (closeDump < 4) {
						++closeDump;
						auto* state = actor->AsActorState();
						logger::info(
							"close actor {:08X} '{}' deadVirt={} gone={} life={} ctrl={} humanoid={}",
							actor->GetFormID(),
							actor->GetName(),
							actor->IsDead(),
							ActorIsGone(actor),
							state ? static_cast<std::uint32_t>(state->GetLifeState()) : 99u,
							actor->GetCharController() != nullptr,
							ShouldScaleHull(actor));
					}
					if (!ActorHullReady(actor)) {
						return;
					}
					++usable;
					if (!ShouldScaleHull(actor)) {
						return;
					}
					++humanoid;
					if (InPlayersFight(actor, logPlayer)) {
						++fight;
					}
				});
				logger::info(
					"fight hulls phantom={} tracked={} close={} usable={} humanoid={} fight={}",
					g_phantomCount,
					g_tracked.size(),
					close,
					usable,
					humanoid,
					fight);
			}
		}

		DrawDebug();
	}
}
