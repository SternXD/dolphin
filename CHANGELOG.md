# Changelog — SternXD/dolphin `uwp-triforce` branch

All changes relative to SternXD/dolphin `master` (v1.1.9.0 baseline).

---

## [1.1.9.1-triforce] — 2026-06-09 (rev 3)

### Summary

65 Triforce cherry-picks from upstream dolphin-emu were applied to the `uwp-triforce` branch on top of SternXD `master`. The result boots Triforce arcade games on Xbox Series X in Developer Mode. Virtua Striker 2002 and Virtua Striker 4 Ver.2006 are confirmed working.

---

### Cherry-picks applied (upstream dolphin-emu)

A total of 65 commits were cherry-picked to add Triforce arcade emulation. Key dependency commits among them:

| Commit | Description |
|--------|-------------|
| `97ad5ad1a1` | `Common/StringUtil`: add `SplitStringIntoArray` template |
| `d7c3513eae` | `DiscIO/CachedBlob`: new cached-blob layer for disc images |
| `405baed805` | `Common/DirectIOFile`: new direct-IO file abstraction |
| `5d2e93fa3e` | `Common/Network`: add `SetPlatformSocketOptions` and `SEND_FLAGS` constant |
| `fbb864a0b5` | `Common/MemArena`: add `EnsureMemoryPagesWritable` method |

---

### Build fixes (MSVC / UWP compatibility)

These source changes were required to compile the cherry-picked Triforce code with MSVC 14.51 (VS 2025 toolset v145) for the UWP/Xbox target.

#### `Source/Core/Common/BitUtils.h`

- **`BitCastPtrType::operator=`** — Replaced SFINAE `std::enable_if` guard with C++20 `requires(!std::is_const_v<PtrType>)`. The old form rejected implicit `int`→`u16` conversions that the Triforce code relies on.
- **`SetBit` overloads** — Added `constexpr` to both `SetBit(T&, size_t, bool)` and `SetBit<bit_number>(T&, bool)`. Required because `AMMediaboard::GuestFdSet::SetFd` is `constexpr` and calls `SetBit`.

#### `Source/Core/Common/Network.h` / `Network.cpp`

- Cherry-picked `SEND_FLAGS` constant and `SetPlatformSocketOptions` from commit `5d2e93fa3e`.
- Resolved merge conflict in `Network.cpp`: kept upstream's include section (removed legacy `<string.h>` and `<sys/types.h>` from the non-Windows block).
- Added `#include "Common/Swap.h"` to `Network.cpp` to resolve missing `BigEndianValue` symbol.

#### `Source/Core/DiscIO/Volume.h`

- Added `#include <span>`.
- Added `std::span<const char>` overload for `DecodeString` (alongside the existing `const char (&)[N]` template). Required because Triforce code calls `DecodeString(boot_id->game_name)` where `game_name` is `std::array<char, 0x20>`.

#### `Source/Core/Common/StringUtil.h` / `StringUtilTest.cpp`

- Cherry-picked `SplitStringIntoArray` from commit `97ad5ad1a1`.
- Resolved conflict in `StringUtilTest.cpp` by keeping the existing SternXD test content.

#### `DolphinLib.props`

- Registered `CachedBlob.h`/`CachedBlob.cpp` (commit `d7c3513eae`) and `DirectIOFile.h`/`DirectIOFile.cpp` (commit `405baed805`) in the vcxproj props file.

#### `Source/DolphinWinRT/DolphinWinRT.vcxproj`

- Added VS 2025 toolset condition so the project builds with MSVC 14.51:
  ```xml
  <PlatformToolset Condition="'$(VisualStudioVersion)' == '18.0'">v145</PlatformToolset>
  ```

#### `Externals/lunasvg/plutovg/plutovg.vcxproj` / `lunasvg.vcxproj`

- Removed `WholeProgramOptimization` (unsupported in this toolset configuration).
- Added `PreferredToolArchitecture=x64`.
- Disabled optimization (`Disabled`), intrinsics, and PDB generation to work around VS 2025 codegen issues with these external projects.

---

### Triforce input mapping — Xbox controller (`Source/Core/Core/HW/GCPadEmu.cpp`)

Inside the `#if WINRT_XBOX` block, the Triforce button assignments previously hardcoded keyboard keys (`1`, `2`, `3`) that do not exist on an Xbox controller. Replaced with WGInput expressions matching the pattern used by all other Xbox mappings in the same block:

| Triforce input | Before | After (Xbox) |
|----------------|--------|--------------|
| Test | `` `1` `` | `Thumb R` (right stick click / RS) |
| Service | `` `2` `` | `Bumper L` (left bumper / LB) |
| Coin | `` `3` `` | `Thumb L` (left stick click / LS) |

`Thumb R` was chosen for Test after confirming `View` (back button) does not reliably pass through to the app on Xbox. Service (LB) and Coin (LS click) were confirmed working on Virtua Striker 2002. All other buttons (A/B/X/Y, RB, Menu/Start, D-pad, triggers, right stick axes, rumble) are used for gameplay.

---

### PanicAlert removal (`AMMediaboard.cpp`, `SI_DeviceAMBaseboard.cpp`)

On Xbox there is no keyboard or mouse to dismiss blocking `PanicAlertFmt` dialogs — they hang the app permanently. All `PanicAlertFmt`/`PanicAlertFmtT` calls in the two Triforce files were converted to `WARN_LOG_FMT`.

#### `Source/Core/Core/HW/DVD/AMMediaboard.cpp` — 9 alerts removed

| Was | Now |
|-----|-----|
| `PanicAlertFmt("Failed to open/create: trinetcfg.bin")` etc. (5×) | `WARN_LOG_FMT(AMMEDIABOARD, ...)` |
| `PanicAlertFmt("Failed to read: segaboot.gcm")` | `WARN_LOG_FMT(AMMEDIABOARD, ...)` |
| `PanicAlertFmtT("Unhandled Media Board Read: ...")` (2×) | `WARN_LOG_FMT(AMMEDIABOARD, ...)` |
| `PanicAlertFmtT("Unhandled Media Board Command/Write/Execute: ...")` (4×) | `WARN_LOG_FMT(AMMEDIABOARD, ...)` |
| `PanicAlertFmtT("Unknown game ID: ...")` | `WARN_LOG_FMT(AMMEDIABOARD, ...)` |

The "Unhandled" alerts are development-mode guards that fire on code paths encountered during normal VS4 boot, making them the most likely crash cause for games that work on PC but not on Xbox.

#### `Source/Core/Core/HW/SI/SI_DeviceAMBaseboard.cpp` — 6 alerts removed

| Was | Now |
|-----|-----|
| `PanicAlertFmt("JVSIOMessage overrun!")` (3×) | `WARN_LOG_FMT(SERIALINTERFACE, ...)` |
| `PanicAlertFmt("JVSIOMessage: Not enough space for checksum!")` | `WARN_LOG_FMT(SERIALINTERFACE, ...)` |
| `PanicAlertFmt("SI: Unknown command")` | `WARN_LOG_FMT(SERIALINTERFACE, ...)` |
| `PanicAlertFmt("SI: Unknown direct command")` | `WARN_LOG_FMT(SERIALINTERFACE, ...)` |

### Triforce data files — required for VS4

VS4 Ver.2006 requires **MAIN BOOT firmware version 2.17+** and a pre-initialized DIMM state. Using a generic `segaboot.gcm` (e.g. from retrobios) causes a permanent "WARNING: BOOT/FIRM VERSION DOES NOT FULFILL THE GAME SPEC / PLEASE ENTER GAME TEST AND UPDATE" block on every boot because it carries an older firmware version that doesn't satisfy VS4's requirement.

**The correct set of files comes from a working Dolphin Triforce session** (e.g. the Android build) where VS4 has already completed its SYSTEM UPDATE. Copy the entire `Triforce/` user data folder from that session:

| File | Purpose |
|------|---------|
| `segaboot.gcm` | Sega Triforce firmware — must be version 2.17+ for VS4 |
| `tridimm_GLLP6E.bin` | DIMM state for VS4 Ver.2006 — contains the completed firmware update result; without this VS4 prompts for SYSTEM UPDATE on every boot |
| `tribackup_GLLP6E.bin` | VS4 backup/NVRAM save |
| `trinetcfg.bin` / `trinetctrl.bin` / `triextra.bin` | Network and extra config — can be empty (0 bytes) |

Upload all files to the Dolphin `Triforce/` user data directory via Xbox Device Portal:

```bash
for FILE in /path/to/triforce/*; do
  FNAME=$(basename "$FILE")
  curl -sk --tlsv1.3 -X POST \
    "https://<xbox-ip>:11443/api/filesystem/apps/file?knownfolderid=LocalAppData&packagefullname=<pkg>&path=\LocalState\Triforce" \
    -H "X-CSRF-Token: $CSRF" -H "Cookie: CSRF-Token=$CSRF" \
    -F "$FNAME=@$FILE;type=application/octet-stream"
done
```

### Dolphin user directory (Xbox) — `user.txt`

The Dolphin WinRT app resolves its user data path at startup via `UWP::GetUserLocation()`:

1. Reads `LocalState/user.txt`
2. If present, uses its content as the user directory path
3. If absent, defaults to `LocalState/`

The user directory determines where `Config/`, `Triforce/`, `GC/`, etc. are stored. WDP's file manager API (`knownfolderid=LocalAppData`) can only write to `LocalState/` — it cannot write to arbitrary drive paths (e.g. `E:\`). If `user.txt` points to an external drive, upload the Triforce files there via another method, or redirect `user.txt` to LocalState:

```bash
# Redirect user dir back to LocalState (full Q:\ path required)
printf 'Q:\\Users\\UserMgr4\\AppData\\Local\\Packages\\<pkg-no-version>\\LocalState' > user.txt
curl -sk --tlsv1.3 -X POST \
  "https://<xbox-ip>:11443/api/filesystem/apps/file?knownfolderid=LocalAppData&packagefullname=<pkg>&path=\LocalState" \
  -H "X-CSRF-Token: $CSRF" -H "Cookie: CSRF-Token=$CSRF" \
  -F "user.txt=@user.txt;type=application/octet-stream"
```

`ISOPath0` in `LocalState\Config\Dolphin.ini` still points to `E:\GC\Triforce` so the game library is unaffected.

### Triforce button mapping UI (`Source/Core/UICommon/ImGuiMenu/ImGuiMappingWindow.cpp`)

The Xbox ImGui controller mapping dialog previously rendered only: Buttons, Main Stick, C-Stick, Triggers, D-Pad. The Triforce group (`m_triforce` — Test/Service/Coin) was defined but not exposed.

Added one line to the GameCube tab render block:

```cpp
renderGroupUI(findGroup("Triforce"));
```

The Triforce section now appears at the bottom of the controller mapping dialog, matching the Android Dolphin UI. Users can remap Test/Service/Coin to any controller input at runtime without a rebuild.

---

### Deployment (Xbox Series X, Developer Mode)

Packages are deployed via the Xbox Device Portal (WDP) REST API over HTTPS on port 11443.

**Key findings:**

- **TLS 1.3 required for large uploads** — WDP on Xbox uses TLS 1.2 by default; uploading a ~14 MB MSIX over TLS 1.2 triggers an SSL renegotiation that resets the connection mid-transfer. Passing `--tlsv1.3` to curl bypasses this.
- **CSRF token** — All `POST`/`DELETE` requests require a valid CSRF token obtained from the `Set-Cookie` header of a prior `GET` to the WDP root, sent as both a cookie and an `X-CSRF-Token` request header.
- **Uninstall before reinstall** — If the same package version is already installed, the install API returns error `0x80070057` (parameter incorrect). Uninstall via `DELETE /api/app/packagemanager/package?package=<fullname>` first.
- **Launch from Xbox home** — Launching via the WDP UI (`/api/taskmanager/app`) returns `0x80070002`; launching the app directly from the Xbox home screen works correctly.

---

### Result

**Virtua Striker 2002** and **Virtua Striker 4 Ver.2006** both boot and run on Xbox Series X in Developer Mode.

| Control | Xbox button |
|---------|------------|
| Coin insert | LS click (left stick click) |
| Service | LB |
| Test / enter test menu | RS click (right stick click) |

Triforce button assignments are remappable at runtime via Settings → Controls → GameCube → Map → Triforce section. Test menu requires `segaboot.gcm` and a pre-initialized `tridimm_<gameid>.bin` in the Triforce user data directory.
