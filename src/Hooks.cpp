#include "Hooks.h"

#include "Collision.h"

namespace Hooks
{
	namespace
	{
		void PlayerUpdate(RE::Actor* a_this, float a_delta);

		REL::Relocation<decltype(PlayerUpdate)> _PlayerUpdate;

		void PlayerUpdate(RE::Actor* a_this, float a_delta)
		{
			_PlayerUpdate(a_this, a_delta);
			Collision::Update(a_delta);
		}
	}

	void Install()
	{
		REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_PlayerCharacter[0] };
		_PlayerUpdate = vtbl.write_vfunc(0xAD, PlayerUpdate);
		logger::debug("PlayerCharacter::Update hooked");
		Collision::InstallProxyHooks();
	}
}
