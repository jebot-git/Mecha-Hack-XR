# Mecha Hack XR

Pilot a mech with two tracked controllers in a branching campaign of nine contracts and a finale. Missions combine procedural terrain, enterable structures, faction encounters and dynamic weather. The game supports solo play and co-op for up to four pilots.

## Play

Download the Linux archive, Windows ZIP or signed Quest 3 APK from [Releases](https://github.com/jebot-git/Mecha-Hack-XR/releases). Extract the desktop packages before launching. On Linux, run `./mecha-hack-xr` with an active OpenXR runtime. On Windows, run `Start-XR.bat`. Sideload the APK on Quest 3.

Create or select a pilot in the hangar. Choose **Online Co-op Lobby** to enter the shared public party. The first pilot hosts. The game server is at `87.106.27.237:29597` and voice uses UDP 29598. Leave through the hangar to return to solo play. Solo is the default and works offline.

Navigate menus with the left stick. A or the right trigger confirms, B or the left trigger goes back. In a mission the menu button pauses. Desktop preview is available with `--desktop`. See [controls](docs/MECHA_INTERFACE.md).

## Build

The repository includes the published game assets. Linux builds require a C++20 compiler, CMake, Ninja, Python 3, SDL2, OpenXR, desktop OpenGL, EGL, X11 and Opus development packages.

```sh
python3 packaging/portable/fetch-deps.py --platform linux
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DIRON_DEPS_DIR="$PWD/.deps/src"
cmake --build build --target mecha-hack-xr mecha-hack-server
./build/mecha-hack-xr --xr --assets "$PWD/assets/mecha"
```

To build just the dedicated server, configure with `-DIRON_BUILD_CLIENT=OFF`. To run a private server, start `mecha-hack-server --bind 0.0.0.0 --port 29607 --assets assets/mecha` and use `--join HOST:29607` on each client. Allow the selected UDP port and the following port for voice.

For Windows, fetch dependencies with `--platform windows` and add `-DCMAKE_TOOLCHAIN_FILE=packaging/mecha/windows-x64.cmake` to a fresh build directory. For Quest, fetch with `--platform quest`, select the Android NDK toolchain with `-DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 -DANDROID_STL=c++_shared` and set `IRON_OPENXR_ANDROID` to `.deps/src/openxr-android` by absolute path. Build the `mecha-hack-xr` target.

Run `python3 packaging/mecha/package.py --help` for the desktop and APK packaging options. The Quest packager uses JDK 17, Android platform 34, build-tools 36.1.0 and a signing key supplied by the builder.

## Credits

The setting draws on *The Mecha Hack* and *Mission Manual*. Game art, campaign arrangements and sound production credits are included in the asset directories. See [weapon and radio credits](assets/mecha/fx/CREDITS.md), [soundtrack credits](assets/mecha/ost/CREDITS.md), [spatial audio data](data/hrtf/LICENSE.txt) and [third-party notices](licenses).
