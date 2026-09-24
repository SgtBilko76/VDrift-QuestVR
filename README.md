# VDrift VR — VDrift on Meta Quest (standalone, OpenXR)

[![Sponsor](https://img.shields.io/badge/Sponsor-SgtBilko76-ea4aaa?logo=githubsponsors&logoColor=white)](https://github.com/sponsors/SgtBilko76)


A native Android port of [VDrift](https://vdrift.net/) for Meta Quest headsets. VDrift's own
shader-based GL2 renderer runs on **OpenGL ES 3.2** directly (no gl4es); stereo rendering, head
tracking and controller input go through OpenXR using the Team Beef framework from
[QuakeQuest](https://github.com/Team-Beef-Studios/QuakeQuest), the same base as
[SpeedDreamsVR](../SpeedDreamsVR) and [TorcsVR](../TorcsVR) next door.

| Input | Menus | Race |
|---|---|---|
| Right hand pointer | move the cursor on the floating screen | — |
| Right trigger | click | throttle |
| Left trigger | back | brake |
| Left thumbstick | up/down/left/right in lists | steering |
| A | select / Enter | start engine |
| B / left menu button | back (Escape) | pause menu |
| X | — | handbrake |
| Y | recenter the view (hold 1 s) | next camera; hold 1 s to recenter |
| Right grip / left grip | — | shift up / shift down |
| Left stick click | — | reverse gear |
| Right stick click | — | rollover recover |

The race is rendered in stereo with head tracking on whatever camera VDrift has selected
(in-car, hood, chase, ...). The HUD is composited as a head-locked panel; the menus are a
floating screen in front of you (curved when the runtime supports cylinder layers), drawn
over the 3D garage scene.

## Status

Runs on a Quest 3 (Horizon OS, Adreno 740): menus on the floating screen, garage scene in
stereo behind them, races in stereo with head tracking on every camera, the HUD as a
head-locked panel, sound, rumble. The minimal data set (4 tracks, 5 cars) holds the 72 Hz
refresh rate at 1.2x supersampling and 4x MSAA.

**Pico** (Pico 4 / 4 Ultra / Neo 3, PICO OS 5): the same APK. The manifest carries the
PICO OS entries next to the Meta ones, the Khronos OpenXR loader finds the PICO runtime, and
the framework enables `XR_BD_controller_interaction` and the PICO controller bindings when
the runtime offers them. Refresh-rate selection (`refresh`) is Meta-only; PICO runs at its
system rate.

Not done yet: the download manager, translations, multiview rendering (the scene is culled
and drawn once per eye). VDrift's GUI has no text fields, so no on-screen keyboard is needed.

## Layout

| Path | What |
|---|---|
| `../vdrift` | VDrift source (branch `quest-port`, Android changes under `__ANDROID__` and in `src/android/`) |
| `../vdrift/vdrift-data` | VDrift data (SVN checkout of `https://svn.code.sf.net/p/vdrift/code/vdrift-data`) |
| `src/vr` | VR glue: OpenXR framework (`tbxr/`), frame driver + stereo (`vr_stereo.cpp`), controllers (`vr_input.cpp`), paths, config |
| `android/` | Gradle project (AGP 8.2.1, CMake, prefab OpenXR loader) |
| `CMakeLists.txt`, `cmake/` | Native build: one `libvdriftvr.so` with the VR layer, the game and all dependencies |
| `third_party/` | bullet3, ogg, vorbis (git clones), zlib, libpng (vendored) |
| `tools/` | data staging / push / run scripts |
| `templates/` | `vr.cfg` and the VR `controls.config`, seeded onto the headset on first run |

## Build

Requirements: Android SDK with NDK 27.2, CMake 3.22.1 (SDK), Java 17, Python 3.

```powershell
cd E:\vdrift
git checkout quest-port

cd E:\VDriftVR\android
.\gradlew assembleDebug        # -> app\build\outputs\apk\debug\app-debug.apk
.\gradlew assembleRelease      # -> app\build\outputs\apk\release\app-release.apk
```

Without a release key both are signed with the local debug key, so either can be sideloaded
and one upgrades the other in place. The debug key differs per machine, so for releases put
a shared key in `android/keystore.properties` (`storeFile`, `storePassword`, `keyAlias`,
`keyPassword`; ignored by git) and release builds use it.

VDrift is expected next to this repo (`../vdrift`). Elsewhere, pass its path:
`./gradlew assembleRelease -PvdriftRoot=/path/to/vdrift`. The `quest-port` branch is
published at [SgtBilko76/vdrift](https://github.com/SgtBilko76/vdrift/tree/quest-port):

```sh
git clone -b quest-port https://github.com/SgtBilko76/vdrift.git ../vdrift
```

`third_party/` is not in git. To recreate it: clone
[bullet3](https://github.com/bulletphysics/bullet3) (3.25),
[ogg](https://github.com/xiph/ogg) (v1.3.5) and [vorbis](https://github.com/xiph/vorbis) (v1.3.7)
into it, and copy `zlib` and `libpng` from `../SpeedDreamsVR/third_party`.

## Game data

The data is not in the APK. Stage and push it once:

```powershell
python tools\stage_data.py --minimal      # 5 cars, 4 tracks - or no flag for everything (~1.7 GB)
python tools\stage_data.py --cars XS,TL2 --tracks estoril88
.\tools\push-data.ps1 -Full               # adb push to /sdcard/VDriftVR
```

On device the layout is

```
/sdcard/VDriftVR/data/        the VDrift data tree (VDRIFT_DATA_DIRECTORY)
/sdcard/VDriftVR/.vdrift/     user settings ($HOME/.vdrift): VDrift.config, controls.config, replays, logs
/sdcard/VDriftVR/templates/   VR defaults, copied into .vdrift when a file is missing
/sdcard/VDriftVR/vr.cfg       VR tunables (see below)
```

Delete `.vdrift` to reset settings (`push-data.ps1 -Reset`). The app needs "All files access";
grant it in the dialog or with `adb shell appops set com.vdriftvr MANAGE_EXTERNAL_STORAGE allow`.

## Run

```powershell
adb install -r android\app\build\outputs\apk\debug\app-debug.apk
adb shell am start -n com.vdriftvr/.VDriftVRActivity
adb logcat -s VDriftVR:V TBXR:V AndroidRuntime:E DEBUG:E
```

`tools\run.ps1 -Build` does all of the above. VDrift's own log is at
`/sdcard/VDriftVR/.vdrift/logs/log.txt`; its stdout/stderr also go to logcat.

## Multiplayer

Races run on a dedicated Linux server (`tools/server/`, built from `server/CMakeLists.txt`),
which owns the physics: every car, including collisions between players, is simulated
there, and clients receive a state snapshot of every car 30 times a second. The local car
is predicted from the player's own inputs and corrected against the server, remote cars
and bots are shown from the server's state.

In the headset, **Multiplayer** on the main menu picks a server (the list is
`/sdcard/VDriftVR/.vdrift/servers.config`, one `name = host:port` per line; the name
other players see is `player_name` in `vr.cfg`) and connects with the car chosen in the
Garage. The lobby status shows on that page; when the server starts a race the track and
grid load automatically, the race runs in stereo like a single-player one, and after the
results you are back in the garage, still connected, until the next race. Disconnect
leaves the server.

See `tools/server/README.md` for hosting: `setup-linux-server.sh`, `run-server.sh`
(track rotation, laps, bots), a systemd unit, and a join probe for testing without a
headset.

## Settings

Edit `/sdcard/VDriftVR/vr.cfg` and restart the app:

| Key | Default | Effect |
|---|---|---|
| `refresh` | 72 | Display Hz (72/80/90/120). |
| `supersampling` | 1.2 | Eye-buffer scale, as a fraction of the runtime's recommended resolution. |
| `msaa` | 4 | Multisampling on the eye buffers (1/2/4/8), resolved in tile memory. |
| `ui_height` | 1200 | Height of the 2D layer (menus, HUD) in pixels; 4:3. |
| `screen_distance`, `screen_height` | 2.2, 1.7 | Floating menu screen, metres. |
| `hud_distance`, `hud_height`, `hud_drop` | 1.4, 1.15, 0.05 | Head-locked HUD panel, metres. |
| `steer_sensitivity` | 0.6 | Left stick response; 1.0 linear, lower is calmer around centre. |
| `ffb_rumble`, `ffb_rumble_scale`, `ffb_rumble_deadzone` | 1, 1.0, 0.12 | Force feedback as controller rumble. |
| `benchmark` | 0 | 1 starts a race directly (VDrift `-benchmark`), for testing over adb. |

VDrift's own graphics settings (Options → Display) still apply where they make sense; the
renderer is fixed to `gl2/basic.conf`, the resolution to the eye buffer, and antialiasing is done
on the eye buffers by `msaa`.

## How it works

- **GL**: VDrift's GL2 path is all shaders and vertex buffers, so it runs on GLES 3.2 as is.
  `src/android/glcore_gles.h` replaces the generated GL loader with the system headers plus the
  extension flags; `shader.cpp` prepends a `#version 300 es` preamble; `texture.cpp` swizzles
  BGR DDS data; framebuffer 0 is routed to whichever OpenXR swapchain image the VR layer bound
  (`glcDefaultFramebuffer`).
- **No window, no SDL**: `window_android.cpp`, `eventsystem_android.cpp`,
  `forcefeedback_android.cpp` and an AAudio backend in `sound.cpp` replace the SDL platform
  code; `src/android/SDL3/` carries only the keycode headers.
- **Stereo**: `Game::DrawVR` draws the scene once per eye. The OpenXR eye pose relative to a seated
  base is applied in view space (`view_eye = eyeRot⁻¹ · view_cam`), so head tracking works with
  every VDrift camera; the projection is the runtime's asymmetric frustum
  (`GraphicsCamera::tanfov`). The 2D passes (camera `2d`: menus, HUD, text) are drawn into a
  separate swapchain and composited as a quad/cylinder layer.
- **Input**: the Touch controllers are a virtual joystick (index 0) plus a pointer on the menu
  screen and a few synthetic keys; `templates/.vdrift/controls.config` binds them and can be
  changed in Options → Controls like any joystick.
- **Multiplayer**: `src/net` in VDrift: `netprotocol.h` (ENet, two channels), `netclient`
  (the game side), `netserver` + `server_main` (the dedicated server); the server build
  links VDrift's physics/track/AI code with a generated null GL layer.
- **Not available**: the in-game car/track download manager (no libcurl), screenshots
  (`adb exec-out screencap`), gettext translations (menus are in English).

## License

VDrift is GPL-3.0. The Team Beef OpenXR framework files follow QuakeQuest's license (GPL-2.0).
The VR glue in `src/vr` is GPL-3.0.
