#include "MenuUI.h"
#include "Settings.h"
#include "ScaleMath.h"

#include <string>
#include <unordered_map>

#include "third_party/SKSEMenuFramework.h"

namespace MenuUI
{
	namespace
	{
		const char* L(const char* a_key, const char* a_english)
		{
			static std::unordered_map<std::string, std::string> cache;
			const auto it = cache.find(a_key);
			if (it != cache.end()) {
				return it->second.c_str();
			}
			std::string translated;
			if (SKSE::Translation::Translate(a_key, translated) &&
				!translated.empty() &&
				translated != a_key &&
				!translated.starts_with('$')) {
				const auto [ins, _] = cache.emplace(a_key, std::move(translated));
				return ins->second.c_str();
			}
			const auto [ins, _] = cache.emplace(a_key, a_english);
			return ins->second.c_str();
		}

		bool EditBool(const char* a_label, bool& a_value)
		{
			if (!ImGui::Checkbox(a_label, &a_value)) {
				return false;
			}
			Settings::Save();
			return true;
		}

		bool EditScale(const char* a_label, float& a_value)
		{
			if (!ImGui::SliderFloat(
					a_label,
					&a_value,
					ScaleMath::kMinScale,
					ScaleMath::kMaxScale,
					"%.2f")) {
				return false;
			}
			if (!ImGui::IsItemDeactivatedAfterEdit()) {
				return false;
			}
			a_value = ScaleMath::ClampScale(a_value);
			Settings::Save();
			return true;
		}

		void __stdcall Render()
		{
			Settings::SyncFromDisk();

			ImGui::TextWrapped("%s", L("$DCC_SMF_Intro",
				"Bigger collision during fights. Changes here are saved to the same file as MCM Helper."));
			ImGui::Spacing();

			if (ImGui::CollapsingHeader(L("$DCC_Header_General", "General"), ImGuiTreeNodeFlags_DefaultOpen)) {
				EditBool(L("$DCC_Enabled", "Enabled"), Settings::bEnabled);
				ImGui::TextWrapped("%s", L("$DCC_Enabled_Help",
					"When off, all actors return to vanilla collision."));
				EditBool(L("$DCC_LockOn", "Lock-on only"), Settings::bLockTargetOnly);
				ImGui::TextWrapped("%s", L("$DCC_LockOn_Help",
					"Requires True Directional Movement. While locked, only you and the lock target get bigger collision. Unlock restores both even if combat continues."));
				EditBool(L("$DCC_Ally", "Ally fights"), Settings::bAllyCombatCollision);
				ImGui::TextWrapped("%s", L("$DCC_Ally_Help",
					"When you are not in combat, followers and allies still get combat collision in their own fights. Their hull size follows their right-hand weapon, not yours. Lock-on only still applies to you, not allies."));
				EditBool(L("$DCC_ShowBox", "Show box"), Settings::bDebugDraw);
				ImGui::TextWrapped("%s", L("$DCC_ShowBox_Help",
					"Green ring is the real collision width. Requires TrueHUD. Turn off after testing."));
				EditBool(L("$DCC_NoPush", "Don't get pushed"), Settings::bPlayerImmovable);
				ImGui::TextWrapped("%s", L("$DCC_NoPush_Help",
					"NPCs stop on you. They will not slide you."));
				EditBool(L("$DCC_AttackTrans", "Attack translation"), Settings::bTranslationHelper);
				ImGui::TextWrapped("%s", L("$DCC_AttackTrans_Help",
					"Lets NPC stepping attacks keep moving until the rings touch."));
			}

			if (ImGui::CollapsingHeader(L("$DCC_Header_Weapons", "Weapons"), ImGuiTreeNodeFlags_DefaultOpen)) {
				EditScale(L("$DCC_Default", "Default"), Settings::fCombatScale);
				ImGui::TextWrapped("%s", L("$DCC_Default_Help",
					"Bows, one-handed axes/maces, magic, and any type without its own slider. Enemies copy your right-hand size. Anyone who is not an enemy stays at 1.50."));
				EditScale(L("$DCC_Fist", "Fist"), Settings::fFist);
				EditScale(L("$DCC_Dagger", "Dagger"), Settings::fDagger);
				EditScale(L("$DCC_Sword", "Sword"), Settings::fSword);
				EditScale(L("$DCC_Longsword", "Longsword"), Settings::fLongsword);
				EditScale(L("$DCC_Warhammer", "Warhammer"), Settings::fWarhammer);
				EditScale(L("$DCC_Battleaxe", "Battleaxe"), Settings::fBattleaxe);
			}
		}
	}

	void Register()
	{
		if (!SKSEMenuFramework::IsInstalled()) {
			logger::info("SKSE Menu Framework not installed - using MCM only");
			return;
		}

		SKSEMenuFramework::SetSection(L("$DCC_ModName", "Dynamic Combat Collision"));
		SKSEMenuFramework::AddSectionItem(L("$DCC_SMF_Settings", "Settings"), Render);
		logger::info("Registered SKSE Menu Framework settings page");
	}
}
