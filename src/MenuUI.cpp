#include "MenuUI.h"
#include "Settings.h"
#include "ScaleMath.h"

#include <filesystem>

#include "third_party/SKSEMenuFramework.h"

namespace MenuUI
{
	namespace
	{
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

			ImGui::TextWrapped(
				"Bigger collision during fights. Changes here are saved to the same file as MCM Helper.");
			ImGui::Spacing();

			if (ImGui::CollapsingHeader("General", ImGuiTreeNodeFlags_DefaultOpen)) {
				EditBool("Enabled", Settings::bEnabled);
				ImGui::TextWrapped(
					"When off, all actors return to vanilla collision.");
				EditBool("Lock-on only", Settings::bLockTargetOnly);
				ImGui::TextWrapped(
					"Requires True Directional Movement. While locked, only you and the lock target get bigger collision.");
				EditBool("Ally fights", Settings::bAllyCombatCollision);
				ImGui::TextWrapped(
					"Followers get combat collision in their own fights, sized from their weapon. Lock-on only still applies to you.");
				EditBool("Show box", Settings::bDebugDraw);
				ImGui::TextWrapped(
					"Green ring is the real collision width. Requires TrueHUD.");
				EditBool("Stop at walls", Settings::bShrinkWhenPinched);
				ImGui::TextWrapped(
					"During fights, keep vanilla stop distance against walls, fences, and clutter. Collision vs people stays big. Turn this off and the bigger hull hits the world too.");
			}

			if (ImGui::CollapsingHeader("Weapons", ImGuiTreeNodeFlags_DefaultOpen)) {
				EditScale("Default", Settings::fCombatScale);
				ImGui::TextWrapped(
					"Bows, axes, magic, and types without their own slider.");
				EditScale("Fist", Settings::fFist);
				EditScale("Dagger", Settings::fDagger);
				EditScale("Sword", Settings::fSword);
				EditScale("Longsword", Settings::fLongsword);
			}
		}
	}

	void Register()
	{
		if (!SKSEMenuFramework::IsInstalled()) {
			logger::info("SKSE Menu Framework not installed - using MCM only");
			return;
		}

		SKSEMenuFramework::SetSection("Dynamic Combat Collision");
		SKSEMenuFramework::AddSectionItem("Settings", Render);
		logger::info("Registered SKSE Menu Framework settings page");
	}
}
