#include "MenuUI.h"
#include "Settings.h"
#include "ScaleMath.h"

#include <cstdint>
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
			ImGui::SliderFloat(
				a_label,
				&a_value,
				ScaleMath::kMinScale,
				ScaleMath::kMaxScale,
				"%.2f");
			if (!ImGui::IsItemDeactivatedAfterEdit()) {
				return false;
			}
			a_value = ScaleMath::ClampScale(a_value);
			Settings::Save();
			return true;
		}

		bool EditInt(const char* a_label, std::int32_t& a_value, std::int32_t a_min, std::int32_t a_max, const char* a_format)
		{
			ImGui::SliderInt(a_label, &a_value, a_min, a_max, a_format);
			if (!ImGui::IsItemDeactivatedAfterEdit()) {
				return false;
			}
			if (a_value < a_min) {
				a_value = a_min;
			}
			if (a_value > a_max) {
				a_value = a_max;
			}
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
					"Followers and teammates get combat collision too. Off, anyone who is not hostile to you stays vanilla, even in the same fight. Their hull size follows their right-hand weapon, not yours. Lock-on only still applies to you, not allies."));
				EditBool(L("$DCC_NpcOwnWeapon", "NPC scale on their own weapon"), Settings::bNpcOwnWeapon);
				ImGui::TextWrapped("%s", L("$DCC_NpcOwnWeapon_Help",
					"When on, each NPC uses their right-hand weapon slider. When off, enemies copy your right-hand size. You always use your own weapon."));
				{
					const char* capFmt = Settings::iCombatNpcCap >= 11 ? "All" : "%d";
					EditInt(
						L("$DCC_NpcCap", "NPCs with hull"),
						Settings::iCombatNpcCap,
						1,
						11,
						capFmt);
				}
				ImGui::TextWrapped("%s", L("$DCC_NpcCap_Help",
					"How many NPCs besides you get bigger collision. 1-10 = closest in the fight. 11 = all. You always get a hull. Lock-on only ignores this and uses only the lock target."));
				EditBool(L("$DCC_ShowBox", "Show box"), Settings::bDebugDraw);
				ImGui::TextWrapped("%s", L("$DCC_ShowBox_Help",
					"Green ring is the real collision width. Requires TrueHUD. Turn off after testing."));
				EditBool(L("$DCC_NoPush", "Don't get pushed"), Settings::bPlayerImmovable);
				ImGui::TextWrapped("%s", L("$DCC_NoPush_Help",
					"NPCs stop on you. They will not slide you."));
				EditBool(L("$DCC_AttackTrans", "Attack translation"), Settings::bTranslationHelper);
				ImGui::TextWrapped("%s", L("$DCC_AttackTrans_Help",
					"Lets NPC stepping attacks keep moving until the rings touch."));
				EditBool(L("$DCC_WorldClip", "Doorframes"), Settings::bWorldClipHelper);
				ImGui::TextWrapped("%s", L("$DCC_WorldClip_Help",
					"Keeps oversized combat collision from sticking in doors and furniture. On costs a bit more performance."));
			}

			if (ImGui::CollapsingHeader(L("$DCC_Header_Weapons", "Weapons"), ImGuiTreeNodeFlags_DefaultOpen)) {
				EditScale(L("$DCC_Default", "Default"), Settings::fCombatScale);
				ImGui::TextWrapped("%s", L("$DCC_Default_Help",
					"Bows, magic staves, and any type without its own slider. Enemies copy your right-hand size unless NPC scale on their own weapon is on. Anyone who is not an enemy stays at 1.50."));
				EditScale(L("$DCC_Fist", "Fist"), Settings::fFist);
				EditScale(L("$DCC_Dagger", "Dagger"), Settings::fDagger);
				EditScale(L("$DCC_Axe", "Axe"), Settings::fWarAxe);
				EditScale(L("$DCC_Mace", "Mace"), Settings::fMace);
				EditScale(L("$DCC_Sword", "Sword"), Settings::fSword);
				EditScale(L("$DCC_Longsword", "Longsword"), Settings::fLongsword);
				EditScale(L("$DCC_Warhammer", "Warhammer"), Settings::fWarhammer);
				EditScale(L("$DCC_Battleaxe", "Battleaxe"), Settings::fBattleaxe);
			}

			if (ImGui::CollapsingHeader(L("$DCC_Header_AA", "Animated Armoury"), ImGuiTreeNodeFlags_DefaultOpen)) {
				ImGui::TextWrapped("%s", L("$DCC_Header_AA_Help",
					"Used when the weapon has Animated Armoury / New Armoury keywords. A spear that is only a two-handed sword in the plugin still uses Longsword."));
				EditScale(L("$DCC_Polearm", "Polearm"), Settings::fPolearm);
				EditScale(L("$DCC_Quarterstaff", "Quarterstaff"), Settings::fQuarterstaff);
				EditScale(L("$DCC_Rapier", "Rapier"), Settings::fRapier);
				EditScale(L("$DCC_Katana", "Katana"), Settings::fKatana);
				EditScale(L("$DCC_Claw", "Claw"), Settings::fClaw);
				EditScale(L("$DCC_Whip", "Whip"), Settings::fWhip);
			}
		}
	}

	void Register()
	{
		if (!SKSEMenuFramework::IsInstalled()) {
			return;
		}

		SKSEMenuFramework::SetSection(L("$DCC_ModName", "Dynamic Combat Collision"));
		SKSEMenuFramework::AddSectionItem(L("$DCC_SMF_Settings", "Settings"), Render);
	}
}
