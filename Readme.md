# DolphinUWP - A GameCube and Wii Emulator

![GitHub Actions Workflow Status](https://img.shields.io/github/actions/workflow/status/SternXD/dolphin/buildwinrt.yml)
[![Discord Server](https://img.shields.io/discord/1007582798598647889?color=%235CA8FA&label=Xbox%20Emulation%20Hub&logo=discord&logoColor=white)]([https://discord.com/invite/emulation-collective-1007582798598647889](https://discord.gg/emulation-collective-1007582798598647889))

Dolphin is an emulator for running GameCube and Wii games on Windows,
Linux, macOS, recent Android devices, and UWP/Xbox (Because of this fork). It's licensed under the terms
of the GNU General Public License, version 2 or later (GPLv2+).

Please read the [FAQ](https://dolphin-emu.org/docs/faq/) before using Dolphin.

## System Requirements

### UWP (Universal Windows Platform)

Recommended: Xbox Series S or Xbox Series X

Minimum: Xbox One, Xbox One S, or Xbox One X (expect games to not run well on these platforms) 

## Building for Windows

Use the solution file `Source/dolphin-emu.sln` to build DolphinUWP on Windows.
DolphinUWP targets the latest MSVC shipped with Visual Studio or Build Tools.
Other compilers might be able to build DolphinUWP on Windows but have not been
tested and are not recommended to be used. Git and latest Windows SDK must be
installed when building. If you need more help go to the building guide [here](https://wiki.xbdev.store/en/CompilingDolphin)

Make sure to pull submodules before building:
```sh
git submodule update --init --recursive
```

The "Release" solution configuration includes performance optimizations for the best user experience but complicates debugging Dolphin.
The "Debug" solution configuration is significantly slower, more verbose and less permissive but makes debugging Dolphin easier.

Credits:

[Dolphin](https://dolphin-emu.org/): for their amazing work on [Dolphin Emulator](https://dolphin-emu.org/)
[SirMangler](https://github.com/SirMangler): for their amazing work on porting Dolphin for UWP
[worleydl](https://github.com/worleydl): For his [oct2024-rebase](https://github.com/worleydl/dolphin-color/tree/oct2024-rebase) branch (it helped so much)

Dolphin Emulator is licensed under GPLv2+ and is not associated with Nintendo.