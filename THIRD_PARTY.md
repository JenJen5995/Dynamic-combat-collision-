# Third-party notices

This plugin is MIT, copyright typiak (`LICENSE`). Other licenses still apply.

## CommonLibSSE-NG

Build-time. https://github.com/CharmedBaryon/CommonLibSSE-NG (MIT)

## TrueHUD and True Directional Movement (Ershin)

Both projects are **GPL-3.0-or-later WITH a Modding Exception and a GPL-3.0
Linking Exception (with Corresponding Source)**. See `COPYING` and `EXCEPTIONS`
in each upstream repository.

- `src/TrueHUDAPI.h` — from https://github.com/ersh1/TrueHUD
- `src/TrueDirectionalMovementAPI.h` — from https://github.com/ersh1/TrueDirectionalMovement

Both files are the authors' published API headers and carry the notice
"For modders: Copy this file into your own project if you wish to use this API".
This plugin uses them only to call the published interfaces: `GetTargetLockState`
from True Directional Movement, and widget/draw calls from TrueHUD. No
implementation code from either project is used.

Thin resolver wrappers written for this project: `src/TrueHUD_API.h`, `src/TDM_API.h`

Modding Exception, as published by the author:

```
In addition, as a special exception, the authors gives You the additional right
to link the code of this Program with the existing code that this Program is
intended to be used with or modify and to distribute linked combinations
including the two, subject to the limitations in this paragraph. Modded Code
permitted under this exception may link to the code of this Program without
causing the Modded Code and portion of the combined work corresponding to the
Modded Code to be covered by the GNU General Public License. You must obey the
GNU General Public License in all respects for all of the Program code and
other code used in conjunction with the Program except the Modded Code covered
by this exception. If you modify this file, you may extend this exception to
your version of the file, but you are not obligated to do so. If you do not
wish to provide this exception without modification, you must delete this
exception statement from your version and license this file solely under the
GPL without exception.
```

GPL-3.0 Linking Exception, as published by the author:

```
Additional permission under GNU GPL version 3 section 7

If you modify this Program, or any covered work, by linking or combining it
with Modding Libraries (or a modified version thereof), containing parts
covered by the terms of Modding Library Licenses, the licensors of this Program
grant you additional permission to convey the resulting work. Corresponding
Source for a non-source form of such a combination shall include the source
code for the parts of Modding Libraries used as well as that of the covered
work.
```

## SKSE Menu Framework and Dear ImGui

- `src/third_party/SKSEMenuFramework.h`
- https://github.com/Thiago099/SKSE-Menu-Framework-2 (MIT)
- https://github.com/ocornut/imgui

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

## MCM Helper and SkyUI stubs

- `tools/papyrus-stubs/` — compile stubs, not SkyUI/MCM Helper source
- MCM Helper: https://github.com/Exit-9B/MCM-Helper (MIT)
- SkyUI: https://github.com/schlangster/skyui
