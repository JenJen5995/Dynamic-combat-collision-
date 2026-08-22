# Third-party notices

This plugin’s own source is MIT, copyright typiak (see `LICENSE`). The files
below are copied or adapted from other projects. Their licenses still apply.

## CommonLibSSE-NG

Not vendored. Required at build time.

- https://github.com/CharmedBaryon/CommonLibSSE-NG
- License: MIT
- Copyright: Ryan-rsm-McKenzie, Charmed Baryon, and contributors

## TrueHUD API

- `src/TrueHUDAPI.h`
- https://github.com/ersh1/TrueHUD
- License: MIT
- The header is the public plugin API Ersh provides for other SKSE mods.

## True Directional Movement API

- `src/TrueDirectionalMovementAPI.h`
- https://github.com/ersh1/TrueDirectionalMovement
- License: MIT
- The header is the public plugin API Ersh provides for other SKSE mods.

Thin resolve wrappers around those headers live in `src/TrueHUD_API.h` and
`src/TDM_API.h` and are part of this project.

## SKSE Menu Framework and Dear ImGui

- `src/third_party/SKSEMenuFramework.h`
- SKSE Menu Framework: https://github.com/Thiago099/SKSE-Menu-Framework-2
  (example client header: https://github.com/Thiago099/SKSE-Menu-Framework-2-Example)
- License: MIT

This header is large on purpose. SKSE Menu Framework owns the real Dear ImGui
instance inside its DLL. The client header is a generated trampoline: each
`ImGui::` call is `GetProcAddress` into that DLL. Replacing it with stock
`imgui.h` would not talk to the in-game menu.

It still contains Dear ImGui type names and declarations:

```
The MIT License (MIT)

Copyright (c) 2014-2024 Omar Cornut

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

https://github.com/ocornut/imgui

## MCM Helper and SkyUI stubs

- `tools/papyrus-stubs/MCM_ConfigBase.psc`
- `tools/papyrus-stubs/SKI_ConfigBase.psc`
- `tools/papyrus-stubs/SKI_QuestBase.psc`

These are one-line compile stubs (script name + parent only). They are not a
redistribution of SkyUI or MCM Helper source.

- MCM Helper: https://github.com/Exit-9B/MCM-Helper (MIT)
- SkyUI: https://github.com/schlangster/skyui — SkyUI itself is a separate
  mod with its own license. Players still need SkyUI installed to use the MCM.
