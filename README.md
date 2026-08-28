# Dynamic Combat Collision

SKSE plugin for SE, AE, and VR. Bigger collision in fights. Walls still stop you
at vanilla distance.

Settings are optional. Author: typiak

## Requirements

- [SKSE](https://skse.silverlock.org/) (SKSEVR for VR)
- [Address Library](https://www.nexusmods.com/skyrimspecialedition/mods/32444) ([VR](https://www.nexusmods.com/skyrimspecialedition/mods/58101))

Optional menu (separate MCM file): [SkyUI](https://www.nexusmods.com/skyrimspecialedition/mods/12604) and
[MCM Helper](https://www.nexusmods.com/skyrimspecialedition/mods/53000). SE/AE can also use
[SKSE Menu Framework](https://www.nexusmods.com/skyrimspecialedition/mods/120352) with no ESP.

Optional: True Directional Movement (lock-on only), TrueHUD (debug draw),
Variadic Collision Dynamics, SkyParkour.

Nexus / dist zips:

- `DynamicCombatCollision-<ver>.zip` — SE/AE plugin (DLL + PDB + translations)
- `DynamicCombatCollision-<ver>-vr.zip` — VR plugin (DLL + PDB + translations)
- `DynamicCombatCollision-<ver>-mcm.zip` — optional ESP and MCM (SE/AE and VR)

Other languages: copy `Interface/Translations/DynamicCombatCollision_ENGLISH.txt` to
`DynamicCombatCollision_<LANGUAGE>.txt` (FRENCH, GERMAN, …).

## Build

Visual Studio 2022, [vcpkg](https://github.com/microsoft/vcpkg), and
[CommonLibSSE-NG](https://github.com/CharmedBaryon/CommonLibSSE-NG).

```
copy local.paths.ps1.example local.paths.ps1
# set CommonLibSSEPath and VcpkgRoot
.\build.ps1
```

`local.paths.ps1` is gitignored. The build pins MSVC 14.44.35207 for spdlog.

Or pass `-DCOMMONLIB_SSE_PATH=` / `CommonLibSSEPath_NG` to CMake yourself.
VR: `-DENABLE_SKYRIM_SE=OFF -DENABLE_SKYRIM_AE=OFF -DENABLE_SKYRIM_VR=ON`.

## License

MIT, see [LICENSE](LICENSE). Third-party files: [THIRD_PARTY.md](THIRD_PARTY.md).
