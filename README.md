# Dynamic Combat Collision

SKSE plugin for SE, AE, and VR. Bigger collision during fights so people actually
bump into each other. Walls can still stop you at vanilla distance.

Settings are in MCM Helper. Author: typiak

## Requirements

- [SKSE](https://skse.silverlock.org/) (SKSEVR for VR)
- [Address Library](https://www.nexusmods.com/skyrimspecialedition/mods/32444) ([VR](https://www.nexusmods.com/skyrimspecialedition/mods/58101))
- [SkyUI](https://www.nexusmods.com/skyrimspecialedition/mods/12604)
- [MCM Helper](https://www.nexusmods.com/skyrimspecialedition/mods/53000)

Optional: True Directional Movement (lock-on only), TrueHUD (debug draw),
SKSE Menu Framework (SE/AE menu), Variadic Collision Dynamics, SkyParkour.

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
