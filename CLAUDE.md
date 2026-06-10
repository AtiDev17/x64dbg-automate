# x64dbg-automate

x64dbg plugin that exposes a ZMQ-based automation server for remote control of the debugger.

## Build

**You must use the VS2022 cmake.** The system PATH does not have cmake. Use:

```bash
"/c/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build64 --config Release
```

If you need to regenerate the build system:

```bash
"/c/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" -B build64 -G "Visual Studio 17 2022" -A x64
```

Output: `build64/Release/x64dbg-automate.dp64`

## Build/install/package scripts

Prefer these over manual cmake + copy steps:

- `build-install-64.cmd` / `build-install-32.cmd` — configure, build Release, and copy the plugin (`.dp64`/`.dp32` + `libzmq-mt-4_3_5.dll`) into the local x64dbg dev install at `C:\re\x64dbg_dev\release\x64\plugins\` (or `x32`).
- `package-release.ps1` — runs both build-install scripts, then zips `build32/Release` and `build64/Release` into `release{32,64}-<version>.zip`. The version string (e.g. `0.7.0-lilac_bonnet`) is hardcoded at the top of the script; its codename suffix must match `XAUTO_COMPAT_VERSION` in `src/xauto_cmd.h` (the client/plugin handshake), which must be bumped whenever the wire protocol changes.

## Project structure

- `src/plugin.cpp` — Plugin callbacks, menu UI, event publishing
- `src/xauto_server.cpp` / `.h` — ZMQ server, session management, command handling
- `src/pluginmain.cpp` / `.h` — Plugin entry points and SDK boilerplate
- x64dbg plugin SDK is fetched via CMake into `build64/_deps/x64dbg-src/pluginsdk/`

## Git

Keep all commits authored solely as the repo owner (Darius Houle) — do not add `Co-Authored-By` or other attribution trailers.

## Settings

Stored in x64dbg.ini under `[XAutomate]`. Key settings:
- `Mode` — `"local"` (default) or `"remote"`
- `BindAddress` — Bind address for remote mode (e.g. `0.0.0.0`)
- `ReqRepPort` / `PubSubPort` — Fixed ports for remote mode (0 = random, local only)
