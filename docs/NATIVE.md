# Native Linux port

The native port is in progress. A headless `nolf-server` executable hosts the
original game module over native UDP; a playable native client is still missing.
The CMake targets currently build the platform
adapters, asset/resource loaders, engine file/stream/math/worker/message modules,
native object-module loading, aligned memory pools, and the existing OpenGL
device backend. The complete server library links and runs startup, native SDK
actor lifecycle, original-level box sweeps and live save/restore tests. The
original server game shell starts with local attributes. The original `m01s01` world
starts with 455 live game actors, runs 600 simulation frames, and shuts down
under both sanitizers. A separate fixture enters the original player and runs
120 frames. A separate native UDP peer now loads an original multiplayer world,
preloads models and sound headers, enters its player, and completes the original
game authentication. It also verifies rejection of incorrect weapon and client
checksums. Full player movement, campaign simulation and a playable client remain
release gates. All
266 original class definitions load natively. String, collection, attribute and
query protocol support have separate runtime tests.

The complete original client game module now builds as `libnolf_cshell.so`
with a strict link and native module descriptor. The host can validate and load
it, but a playable client engine is still required to initialize its UI and run
its game callbacks. Window placement, text/pointer input, modifier keys, SDL
controller state, resource access and writable profiles use native services.
The SDK cursor service decodes the imported CRES pointer and selects its
compatible authored image through SDL on X11 and Wayland. Keyboard physical
bindings and logical menu keys are translated in the platform adapter; the
shared input service implements command states, toggles, mouse axes, tracking,
device/binding snapshots and configuration persistence. The original 136
input commands in `defaults.cfg` load and round trip through this service.
A loopback browser fixture verifies the TCP master exchange and multipart UDP
status replies, including split reads, duplicate fragments and incomplete
responses. These checks do not establish a rendered menu or gameplay.

The agreed release target is the full modern game on x86-64 Linux, with
Wayland and X11, OpenGL, controllers, native sound/adaptive music/movies,
and Windows GOTY 1.003 save and LAN interoperability. Shared-source
refactoring takes priority over the historical byte-matching build.
Reconstruction layouts and historical compiler builds are not native ABIs.

## Build and test

Install a C++20 compiler, CMake 3.24+, Ninja, Flex, Bison, and the development headers
for OpenGL and the desired desktop/audio backends. Linux desktop builds also
require pkg-config, FreeType, Fontconfig and installed system fonts, plus
FFmpeg 5+ development libraries (libavformat, libavcodec, libavutil,
libswresample and libswscale). An installed SDL3 3.2+
is used when available; desktop presets otherwise fetch the pinned SDL3
revision. Desktop builds also fetch the pinned GothicKit dmusic revision for
adaptive music and apply `ext/patches/dmusic/` to it; for an offline build,
pass `-DFETCHCONTENT_SOURCE_DIR_DMUSIC=<checkout>` (patched with
`cmake -DPATCH_DIR=ext/patches/dmusic -P build/cmake/ApplyPatches.cmake` run
inside it), or `-DNOLF_MUSIC=OFF`. No build downloads game assets.

GCC and Clang build every project target with `-fno-strict-aliasing`: the
engine and game sources were written for MSVC, which never optimises on
type-based aliasing, and some code still reads objects through layout views.
Without it an optimised build plays cinematics with invisible actors whose
animation keys never fire, while a debug build looks right. Check gameplay
changes in `out/native-release` as well as the debug build.

```sh
make native-debug
make native-test

# No windowing, graphics, audio, Wine, game data or submodules needed:
make native-headless
ctest --preset native-headless

# AddressSanitizer and UndefinedBehaviorSanitizer:
cmake --preset native-sanitize
cmake --build --preset native-sanitize --parallel
ctest --preset native-sanitize
```

Desktop tests render and read back pixels through the real OpenGL device,
including surface uploads, color keys, masks, affine warps, blending,
multisample resolve, GPU state restoration and renderer restart. CPU surfaces
retain their pixels across device changes; the device releases its textures
before detaching the context. Native PCX decoding supports the indexed and
RGB formats used by the original UI and fonts.

The portable DTX loader decodes raw32, DXT1/3/5 and indexed32 textures with
owned mip images and metadata. Its bounded reader uses explicit byte fields;
the [SDK format definitions](https://github.com/jsj2008/lithtech/blob/master/tools/shared/engine/dtxmgr.h)
also describe the fixed header and section records. All 4,160 original textures
decode, and all 16,522 mip dimensions and BGRA CRC32 values match an independent
Pillow comparison. Alpha is preserved for the renderer's material rules.

`ClientTextures` shares owned texture resources between the renderer and the
actual `ILTTexMod` interface. Editing handles retain consistent flags, locks
publish new snapshots, and releasing the last handle restores the disk image.
Bump heights originate from alpha, and color edits regenerate mipmaps. Renderer
visibility follows completed Render calls. `GlTextures` uploads these snapshots
through the device adapter, with bounded residency, revision updates and device
restart recovery. All original textures pass SDK edit/restoration checks and
exact GPU mip readback on X11 and Wayland under both sanitizer compilers. These
are texture checks; the complete scene renderer and original client host remain
unfinished.

Native SPR decoding and `ClientSprites` retain animation names and canonical
sprite controls without storing pointers in 32-bit fields. `ClientScene` now
creates, replaces and advances these sprites; the client base exposes the real
SDK control interface. Seeking and playback use the same quantized frame period,
with pause, loop, final-frame clamping and checked large deltas. All 173 original
sprite files and 1,106 frames pass these checks in both sanitizer builds. One
authored frame, `menu/sprtex/spiral.dtx`, is absent from the imported media and is
reported by the corpus check. `sprite_vertices` prepares billboards and rotated
decals with native dimensions, UV inset, glow sizing, depth bias and convex
polygon clipping. `GlSprites` draws their prepared geometry through the device
adapter. The composed scene sorts sprites with translucent models, draws no-Z
sprites afterward, and samples sprite bounds independently of collision bounds.
Pixel tests cover SDK animation, color keys, blending, static and moving-brush clipping,
depth and camera-relative sprites. All 1,105 available original frames submit
through the scene on X11 and Wayland under both sanitizer compilers. Texture
color-key variants preserve the original SDK pixels, and shared render materials
remain resident across consecutive frames.

Decals clip against owned world-space polygon geometry. Translated and rotated
doors, box-physics brushes and multiple instances of the same source geometry
use their visual transforms; snapshots remain valid after world unload.

`ClientEffects` supplies particle simulation and the SDK particle/line records.
It preserves sentinel iteration, relative positions, strict lifetime expiry,
gravity, never-die and manually updated systems, and the original floor-bounce
behavior. Prepared geometry and `GlEffects` submit colored lines and textured
particle quads through the shared scene queue. Tests cover local transforms,
sprite animation, keys, blending, world depth, camera-relative effects and
removal from model callbacks. The 173 original sprite materials also render as
particles: 1,105 available frames and one authored missing frame on each backend
under both sanitizer compilers.

Canvases expose the actual `ILTCustomDraw` interface. Each callback records owned
geometry, render states and texture pixel versions before GPU submission.
Removal or renderer retirement during the callback discards the recorded draws.
The device adapter implements all five color/alpha operations, blend factors,
texture addressing and depth controls. Pixel checks cover world-space vertices,
solid-canvas ordering, camera-relative drawing and edits between primitives.
The GPU texture cache can bind retained pixel versions without changing SDK
texture identity.

Polygrids use owned signed height bytes, uint16 indices and a native float color
table. Seven SDK entries provide setup, sprite materials, environment maps,
texture transforms, buffer access and fitted position/scale. Allocation checks
the 65,000-sample limit before changing an existing grid. Prepared meshes cover
the full PC grid dimensions and calculate reflection coordinates from the height
gradients. The scene draws full/half grids, unsigned heights, animated and keyed
materials, environment-only and combined maps, fades and camera-relative grids.
Each texture unit has independent GPU state in the shared cache; combined
residency is checked before either unit is bound. The environment pass preserves
vertex/object alpha when combining the base texture, so grid fades remain active.
All 173 original sprite materials also pass polygrid submission checks on both
backends under both sanitizer compilers. These checks use small test grids;
the original client's water effects still need the complete game host.
Fog and full directional/dynamic lighting and material effects remain unfinished.

The shared model loader now names its mesh records as native vertices and
triangles. Each triangle stores three UV/index pairs; vertices store bone
weights, bind positions and normals, matching the
[independent ABC reader's format](https://github.com/haekb/godot-abc-reader/blob/master/Models/ABC.gd).
Validation checks triangle and bone indices and finite geometry before loading.
Renderer poses own their matrices and retain the model through node callbacks;
removal or rebinding rejects an incomplete pose. Native skinning checks cover
all 695 original models, 5,549 mesh LODs, 360,333 vertices and 673,856 triangles.
An independent file reader confirms these totals and consumes every piece
chunk exactly. The scene renderer consumes these canonical records.

`GlModels` submits canonical poses and mesh LODs through the render device,
with skin textures, per-vertex colors, blending, culling and wireframes. It
restores GPU state and supports texture edits and device recreation. Pixel
fixtures pass on X11 and Wayland under both sanitizer compilers. All 695 models
and their 673,856 triangles also pass device submission checks using a shared
test material. Original material assignment, scene cameras, lighting and pass
ordering still need their scene renderer integration.

`CameraView` supplies native projection/view matrices, frustum planes and SDK
pixel rays from one calculation. It normalizes camera quaternions and applies
the same horizontal FOV adjustment for widescreen rays and rendering. Tests
check projection round trips, rotated cameras, near/far culling, portrait and
one-pixel viewports, and the actual `Get3DCameraPt` SDK binding.

The `SceneRenderer` model pass now connects scene cameras and owned model
poses to skin lookup, animated skins, model hooks, hidden pieces, LOD selection
and transparency ordering. It derives culling bounds from evaluated geometry
and reuses those vertices for drawing. Callback guards handle model removal
and renderer retirement. X11 and Wayland pixel checks include camera-relative
view models and skin animation. The composed scene entry point now draws the
static world, models and sprites with shared depth and completed texture visibility.
Camera-relative models and sprites bypass world PVS filtering. Movable brushes
now share the object ordering, using native forward/inverse transforms, source
lightmap coordinates, actual transformed bounds and BSP transparency ordering.
Terrain subsets skip polygons already drawn by the static pass. Effect passes,
full material effects and directional/dynamic model lighting are still pending.
Opaque model skins retain their opacity when texture alpha carries a fullbright
mask; explicit chromakey objects use the separate alpha-test path.

The portable lightmap codec validates the original 555 color and shadow-mask
streams. `WorldLightmaps` owns composition and bordered CPU atlas pages;
`GlLightmaps` owns bounded GPU residency and context recreation. Lighting
storage generations invalidate caches after replacement, and failed composition
publishes no partial updates. All 102 original worlds compose into 771 atlas
pages, with exact GPU pixels checked on X11 and Wayland under both sanitizers.
Seven authored polygons extend past their lightmap tiles; sampling clamps to
their own tile edges.

`GlWorld` draws canonical native BSP polygons through the device adapter,
using PVS groups, authored textures, animated materials, vertex colors and
animated lightmaps. It identifies sky apertures for the ordered sky pass.
Four views in each of the 102 original levels submit 85,194 polygons and
153,990 triangles, including 72,235 lightmapped polygons, on both display
backends under both sanitizers. The corpus reports two authored `default`
material placeholders and one absent `tex/lights/bltb1/blt0028.dtx`; those
surfaces retain their vertex lighting. These rendering checks do not establish
a playable client or complete scene rendering.

The brush corpus additionally submits all 20,183 original BSP models and 1,094
terrain subsets, with 2,331,757 triangles, on both display backends under both
sanitizers. These bulk geometry checks use a small viewport to bound software
overdraw; the pixel fixtures and level snapshots use a larger viewport. Lighting
composition runs once per render transaction and updates on the next transaction.

Sky packet `0x11` now has a shared explicit float32/uint16 codec, typed client
events, ordered late-bound object IDs and the actual `GetSkyDef` SDK binding.
The sky pass uses the level's view-box parallax and draws brush, model, sprite
and particle layers behind foreground depth. Pixel tests cover ordered and
repeated slots, callback removal, apertures, opacity, tint, view position and
unload. Actual native UDP host/peer exchanges under both sanitizer compilers
validate typed sky updates and complete original game authentication.

`ClientSurfaces` binds the SDK surface function table to owned CPU images and
a renderer display interface. Callback scopes select the owning client;
foreign and retired handles fail without dereferencing game-provided pointers.
The bridge preserves the modern renderer's per-draw color keys and screen
opacity, and reads the live framebuffer for screen copies and screenshots.
These SDK calls are tested on X11 and Wayland, including the original splash;
the original client shell still needs its complete engine context.

The native `ClientPhysics` adapter implements the actual `ILTClientPhysics`
interface using scene-owned objects and the shared server/client collision
kernel. Physics scratch data is local to its engine thread, and explicit push
lists retain object identities across callbacks. A sweep that repeats an
unchanged collision response stops as blocked. Local asset checks exercise
1,216 box sweeps across all 102 original worlds, including stair flags; these
checks do not establish the original player controller or full movement.

`ClientScene` owns native objects, cameras, model resources, world geometry,
attachments and the client collision mover. Its model and common interfaces
validate ownership before dispatching SDK calls. Callback scopes retain scene
state through game callbacks that remove objects or destroy their host;
hosts can also retain dynamically loaded callback code with an explicit lease.
Model node evaluation stops after removal or model replacement, and attachment
updates use a bounded queue instead of recursive movement. World changes
retire level objects while keeping local UI objects and cameras alive. Visibility
uses the render BSP's PVS independently of the physics BSP.

The scene binds 49 SDK object, camera, model-node, particle, line, polygrid and segment-query function pointers, with
thread-local callback contexts and native pointer-sized user data. Local asset
checks load all 695 original ABC models and 102 worlds through this scene and
exercise its collision mover and visibility queries. This is an engine component;
the original client shell still needs the complete client host, network object
decoder and media services. Sprite resources and automatic model animation
dimensions use their canonical native scene records.

`ClientEngineBase` implements the shared SDK host behavior and remains abstract
while its renderer, networking and media services are being connected.
It binds the scene and native console/configuration services, resolves game
strings and files, and defers SDK object retirement to frame boundaries. Its
callback scope retains the engine and selects scene, surface, font and input services.
The engine and renderer accept the same owned texture service, so SDK edits
and frame visibility refer to the renderer's actual textures.
Shell creation checks all 208 function-pointer slots and subsystem interfaces;
the current abstract base has compile coverage, with no original-shell runtime
claim. World descriptions use the same bounded header reader as full level
loading without loading geometry for menu listings.

Client messages retain their codec context through nested messages and use
native server-ID lookup, CRES string formatting and the level's compressed
position bounds. Engine and scene ownership remains weak inside that context.
Object and position codecs reject messages from a previous level, including
a restart of the same level, so recycled IDs cannot resolve to new objects.

The client scene also loads baked lighting before activating a world and
provides the four SDK light-animation methods over that owned data. Shared
light properties serve both server and client. Local corpus checks exercise
all 369 original light animations, including frame changes, invalid inputs
and immutable shadow-map flags. The world renderer consumes their owned atlas
snapshots and preserves coherent lighting within each render transaction.

CTest runs X11 tests under Xvfb when available. When Weston is installed,
it also runs window and surface tests in private headless Wayland sessions.
Those compositor fixtures explicitly use Mesa software rendering; they do
not establish physical GPU or input-device coverage. Linux desktop builds
require EGL development files. The `NOLF_WESTON` CMake variable can select
a particular Weston executable. In an existing Wayland session:

```sh
SDL_VIDEODRIVER=wayland out/native-debug/nolf-desktop-tests
```

GCC and Clang builds, sanitizer tests, headless builds and a modern Windows
build are configured in the native CI workflow. Asset-dependent tests are
local and must not upload proprietary resources to public CI.

## Play

After importing your game data (below), build the optimized client and play:

```sh
make native-play
# or, once built:
out/native-release/nolf-client --data out/native-data
```

`make native-play` uses `GAME_DATA` when given, else the first complete import
(one with `resources/`) of `out/native-data` and `out/native-data-resources`.
The log names the OpenGL renderer in use, which on a hybrid-GPU laptop is the
one the desktop picks for the client.

`nolf-client` opens a 1280x720 window on Wayland or X11 (`--fullscreen`,
`--width`, `--height`; the game's display options change it later), plays the
intro movies and music, and runs single-player games against the original
server code in-process. Saves, `autoexec.cfg` and the player profile live in
`$XDG_DATA_HOME/nolf` (or under `$NOLF_USER_HOME` when set). Console variables
follow LithTech's command line: `+SkipTitle 1` goes straight to the menu and
`+runworld worlds/m01s02` starts a level. An import made before the modern data
layer existed needs `make native-update-modern GAME_DATA=...` once.

For automated runs, `--frames N`, `--max-fps RATE`, `--capture N FILE.bmp`
(repeatable) and `--input SCRIPT` drive the real game headlessly; a script line
is `FRAME key CODE down|up` (DirectInput code in hex), `FRAME mouse DX DY`,
`FRAME button N down|up` (1 left, 2 middle, 3 right), or
`FRAME console COMMAND`. `NOLF_NET_TRACE=1` adds the network protocol trace,
session events and a per-second scene summary to the log.
`SDL_VIDEODRIVER=offscreen` renders on the real GPU without a window (pin the
Mesa EGL vendor with `__EGL_VENDOR_LIBRARY_FILENAMES` on hybrid systems).

`defaults.cfg` is read first and `autoexec.cfg` over it: the engine saves only
`+` variables, so shipped settings such as `MouseLook` come from the defaults.

### Rendering parity

The scene renderer follows the original renderer (port reference:
`src/modern/render`) and reads its console variables each frame:

- Models are lit per vertex (`model_lighting`): the world's light grid, moved
  towards the global light's colour where the model sees the sky towards it
  (`ModelApplySun`, `ModelSunVariance`; the level's `StaticSunLight` and the
  game's time of day), then up to `MaxModelLights` dynamic lights and the
  world's static lights that are not `FastLightObjects`, against bind-pose
  normals (and a `NormalRef` node's frame). `Saturate`/`ModelSaturation` scale
  them; `FLAG_NOLIGHT` is fullbright.
- Dynamic lights reach lightmaps (`WorldLightmaps::update`, `LightSaturate`,
  solid and front-only lights), vertex-lit polygons and sprites.
- `Saturate` doubles the lightmap and vertex colours; the global light scale
  multiplies the finished view and the camera's light add (`LightAddPoly`)
  adds to it.
- `FogEnable`, `FogR/G/B`, `FogNearZ/FarZ`, sky fog, `FLAG_FOGDISABLE`, and
  additive/multiply objects fogging to black.
- Blob shadows (`model_shadows`, `MaxModelShadows`, `ModelShadowAlpha`,
  `ModelShadowOffset`) under `FLAG_SHADOW` models with `ShadowEnable`, or with
  `ModelShadowProj` (the Shadows option's Projected, which the shipped menu
  left out) the original's projected shadows: each such model's shape drawn
  from its strongest light that reaches it into a shadow map
  (`ModelShadowProjRes` texels) and laid on the world (`shadow_maps`).
- Detail textures and world environment maps on a third unit (`DetailTextures`,
  `DetailTextureAdd`, `DetailTextureScale`, `EnvMapWorld`), from the DTX's
  `DetailTex`/`EnvMap` link; water's environment map (`EnvMapPolyGrids`).
- Chrome (`EnvMapEnable`, `EnvMapAll`, `EnvScale`, `EnvPanSpeed`) with the
  game's `EnvMap` texture; the sky's cloud shadow (`SetGlobalPanInfo`).
- `FarZ`, `ModelLODOffset`, `DynamicLight`, `FastLight`.

Texture filtering follows `Bilinear`, `Trilinear` and `Anisotropic` (the
original's defaults: bilinear). `SkyScale` and gamma are not applied.

Enhanced lighting (`EnhancedLighting`, the advanced display options' toggle; off
by default, which keeps the original look) needs the OpenGL device's
programmable extension (GLSL 1.20 programs and depth targets, `rdev.h`):

- Models are lit per pixel (`pixel_lighting`) from the same terms -- grid
  ambient, sun, dynamic and static lights -- with normals smoothed from the
  skinned triangles. The strongest of the grid's lights near a model are taken
  out of its flat ambient and given back with their direction, unless the
  world stands between them (lights in their fixtures excepted).
- Every solid model casts soft shadows from its `ShadowLightsPerCaster`
  strongest lights (the sun where it sees the sky, dynamic and static lights)
  that reach it, as deep as each light is bright where the shadow falls, for
  the `MaxShadowCasters` nearest models, `ShadowMapRes` texels across.
  Characters keep a fainter blob where they meet the ground.

## Import locally owned data

Installed game directories need no unpacking utilities. The supplied ISO
uses Inno Setup and ArC/SREP payloads; image import needs `bsdtar`,
`innoextract`, and a native `unarc` with SREP support. Build the pinned
source tools locally with:

```sh
make native-asset-tools
make native-import ASSET_SOURCE=exclude/NOLF_CD.iso
# Or use an installed GOTY directory:
python3 build/import-assets.py /path/to/NOLF --output out/native-data

out/native-debug/nolf-asset-inspect --data out/native-data
out/native-debug/nolf-platform-tests --resources out/native-data
out/native-debug/nolf-file-tests --data out/native-data
out/native-debug/nolf-engine-services-tests --data out/native-data
out/native-debug/nolf-world-lighting-tests --data out/native-data
out/native-debug/nolf-world-geometry-tests --data out/native-data
out/native-debug/nolf-world-state-tests --data out/native-data
out/native-debug/nolf-object-tests --data out/native-data
out/native-debug/nolf-hash-tests --data out/native-data
out/native-debug/nolf-console-tests --data out/native-data
out/native-debug/nolf-model-tests --data out/native-data
out/native-debug/nolf-model-tests --cache-data out/native-data
out/native-debug/nolf-model-tests --service-data out/native-data
out/native-debug/nolf-sound-tests --data out/native-data
out/native-debug/nolf-game-support-tests out/native-data
out/native-debug/nolf-client-physics-tests out/native-data
out/native-debug/nolf-client-scene-tests out/native-data
out/native-debug/nolf-client-message-tests out/native-data
out/native-debug/nolf-texture-tests --data out/native-data --report out/native-textures.tsv
out/native-debug/nolf-client-texture-tests out/native-data
out/native-debug/nolf-sprite-tests out/native-data
out/native-debug/nolf-model-geometry-tests out/native-data
SDL_VIDEODRIVER=x11 xvfb-run -a out/native-debug/nolf-gl-texture-tests out/native-data
```

Imports preserve the original client DLL's size, CRC32 and source SHA-256 before
removing its executable payload. The small `native/compatibility/cshell.dll`
record supplies the retail protocol identity for native peers; it is separate
from the native module's ABI validation. Reimport older data directories that
lack this record. Missing or mismatched metadata fails authentication.

The game-support corpus check parses 19 original ButeMgr files, containing
1,523 tags and 33,855 values. `AnimationsHuman.txt` uses the separate animation
grammar. GCC and Clang ASan/UBSan checks cover this corpus, string ownership and
formatting, attribute copies and traversal callbacks, save/reload and the retail
four-byte-key encryption framing. This does not execute the original game shell.

The default build includes `nolf_object`, the original server gameplay module.
It uses native Flex and Bison to generate the animation lexer and parser.
Windows builds can use [WinFlexBison 2.5.25](https://github.com/lexxmark/winflexbison/releases/tag/v2.5.25);
CI pins and verifies that archive. To run its metadata check directly:

```sh
out/native-debug/nolf-original-module-tests out/native-debug/libnolf_object.so
```

The check loads the real module and validates its SDK class graph. With locally
owned original Windows metadata, it can also compare class names, parents,
flags and property counts and check every authored level property:

```sh
python3 build/native-class-reference.py /path/to/original/Object.lto --output out/original-classes.tsv
out/native-debug/nolf-original-module-tests out/native-debug/libnolf_object.so out/native-data out/original-classes.tsv
out/native-debug/nolf-original-server-tests out/native-debug/libnolf_object.so out/native-data
# Additional original level startup gate (does not simulate frames):
out/native-debug/nolf-original-server-tests out/native-debug/libnolf_object.so out/native-data --full-world
# Original level simulation and local player entry fixtures:
out/native-debug/nolf-original-server-tests out/native-debug/libnolf_object.so out/native-data --frames 600
out/native-debug/nolf-original-server-tests out/native-debug/libnolf_object.so out/native-data --player-frames 120
# Native client physics over the original world corpus (no game shell):
out/native-debug/nolf-client-physics-tests out/native-data
# Optional focused collision regression:
out/native-debug/nolf-client-physics-tests out/native-data worlds/m09s03.dat
```

The reference reader never executes the Windows image. Use the original file;
imported archives have executable payloads zeroed. The local corpus matches all
266 registered classes and decodes 2,869,494 typed properties in 79,968 object
definitions across 102 levels. One authored `DestructableRotatingDoor` is absent
from both class tables. The source loader warns and skips unregistered classes.

The separate server check rejects missing game data, restarts the original
shell three times, and creates and removes original `GameStartPoint` actors
at their authored positions. Its `--full-world` mode then starts `m01s01`
through shell pre-start, world-model and authored actor creation, caching and
post-start, producing 455 live actors. Shutdown passes leak detection under GCC
and Clang sanitizers. The additional frame fixture runs 600 updates (10 game
seconds), ending with 414 actors. The local player fixture enters the original
player and runs 120 updates (2 game seconds), ending with 418 actors. Both pass
GCC and Clang ASan/UBSan with leak detection. The local fixture has no network
peer or movement input and does not establish player movement correctness. Internal
trigger messages carry owned strings, with nested dispatch tests through the
real engine. Timing,
diagnostics, preferences and log files use consolidated platform services;
the query responder uses native address resolution and the engine transport.
Preferences are stored in `settings.cfg` in the user's config directory, with
`language` defaulting to `English`. Log lines append to `modern.log` in the user's
state directory; startup can choose another destination.

The asset-dependent client/server integration check starts separate native
processes, rejects two incorrect checksums, authenticates a valid peer, receives
two seconds of updates, and checks normal server shutdown:

```sh
python3 tests/native/test_client_session_host.py \
  out/native-debug/nolf-server out/native-debug/nolf-client-session-tests \
  out/native-debug/libnolf_object.so out/native-data --logs out/client-server-check
```

`ClientSession` stages file IDs, engine messages and loading acknowledgements;
`ClientTransport` owns the shared network manager; `GameHandshake` handles the
separate game authentication. The peer loads the actual geometry and ABC models
and validates wave headers. Object updates are retained as raw owned packets.
It does not yet run the original client shell, decode and render those objects,
or play media. This check establishes native-to-native authentication with the
source's GOTY protocol; original Windows LAN interoperability remains unverified.

The original client GUI and font libraries also compile natively. Their tests
cover UTF-8 traversal, owned formatting strings, bitmap word wrapping and line
bounds, font replacement and destruction, and a bounded atomic offset cache in
the platform user-cache directory. Cache entries include atlas content CRC,
dimensions, pitch and glyph groups. These checks do not exercise the client
original interface; that still requires the original client shell and engine
integration.

The system-font path now uses separate native font-discovery and rasterization
adapters. Fontconfig selects installed faces on Linux; FreeType owns the font
bytes, metrics and Unicode glyph bitmaps. `ClientFonts` supplies all seven SDK
font functions with scoped ownership, nonreused handles, bounded text images,
per-font spacing, underlines and the original independent foreground/background
transparency masks. Text dimensions do not allocate glyph bitmaps. Native image
composition writes to CPU surfaces or submits one clipped image to the screen.
Font scopes are part of `ClientEngineBase` callback scopes. CPU/GPU text pixels
match on X11 and Wayland under both sanitizer compilers, including renderer
recreation. The original 640x480 splash remains pixel-exact after these checks.
This completes the font service, with original interface initialization still
pending the concrete client host.

Audio decoding and playback use separate platform adapters. The FFmpeg
adapter reads bounded archive extents through callbacks and emits native
floating point samples; the SDL adapter owns the output device and queue.
Source sample limits exclude compressed block padding before resampling.
All 4,799 original WAVE files decode under both sanitizers: 887 PCM, 250 IMA
ADPCM and 3,662 MP3, totaling 684,407,559 frames. Exact seeking covers all 24
authored loops. `AudioClips` retains immutable samples in a bounded 128 MiB
cache, including clips held by callers after cache retirement; the complete
sound corpus fits through this cache without exceeding its budget.

`ClientSounds` supplies the actual sound-manager virtuals and 21 sound SDK
function pointers. Its sample mixer owns voice clocks, authored loops,
priority virtualization, distance/pan/Doppler, occlusion and obstruction.
Device reloads preserve sound identities and borrowed lip-sync PCM. Callback
scopes retain sound state and game code through reentrant teardown. SDK calls
also reach SDL's dummy device in the automated tests; physical audio has not
yet been checked.

The native DSP implements all 18 filter names used by the original filter
catalog, including interpolation, EQ, modulation, delay and reverb. All 30
active authored definitions and 86 parameters render under both sanitizers.
Filters share a 32 MiB mixer budget; room reflections can outlive their source.
These are native effects with the authored controls, not bit-exact Miles/EAX
emulation.

Movies use the same FFmpeg archive input as audio. `ClientVideos` implements
`ILTVideoMgr`: each movie's soundtrack owns an independent output and is the
master clock, the picture drops late frames, and playback falls back to the
wall clock without a device or after a corrupt soundtrack. Fullscreen movies
are letterboxed. All four original Bink movies (874 frames) decode, seek
exactly and play to completion through the SDK calls under both sanitizers.
Texture videos report `LT_NOTINITIALIZED`; no original level places a `Video`
object.

Adaptive music replaces DirectMusic. `ClientMusic` implements
`ILTDirectMusicMgr` from the level control files (`ControlFile` parses their
sections, comments and `#ifdef` conditionals) and schedules segments exactly as
the shipped manager's pending queue did: transitions, ordered segment lists,
loop counts and follow-on intensities. The platform `MusicPerformance` adapter
synthesizes segments, styles and DLS banks with GothicKit dmusic, rendered on
demand into an independent output. The patches in `ext/patches/dmusic/` add
segment-end scheduling and playback progress, make the tempo independent of
the render size, honour authored repeats, and fix its crashes, unbounded curve
expansion and reference leaks on the original data; each level loads into its
own resource scope because music directories reuse bank names for different
instruments. All 6 original control files, 88 intensities and 775 transitions
load, and every intensity renders finite, unclipped audio, leak-free under
both sanitizers. Original levels only send intensity changes: secondary
segments and motifs report `LT_ERROR`. Pause is a no-op, as shipped. How
closely dmusic's synthesis matches DirectMusic has not been compared against
a reference recording.

Saved games written and read by the engine (`sav_SaveFile`,
`sav_RestoreFile`) use the game's logical names under the user data directory,
like the menus' slot checks and save index; host paths and traversal are
refused, since save names arrive from clients.

`NativeClient` completes the SDK: it composes every service above, binds the
remaining frame, session and query entry points, and drives the original
client shell frame by frame. `StartGame` runs the original server module on its
own thread and joins it through the in-process local driver, as the original
single-player client did; `ClientWorldSync` applies the engine's object, sound
and lighting update packets to the scene exactly as the original client read
them. The menus, intro movies, music, single-player levels, dialogue, combat,
death and autosave restore run on the original game code.

The default build also produces the native headless host. With imported data:

```sh
out/native-debug/nolf-server --data out/native-data --module out/native-debug/libnolf_object.so \
  --world worlds/multi/deathmatch/001thepad_dm --mode dm --port 27888 --players 16
```

Use `--help` for level rotation, configuration, logging and bind address options.
It hosts the actual UDP transport and original game module. Master-server
listing is disabled. Ctrl+C and SIGTERM request normal shutdown on the engine
thread through the platform termination adapter. `--ticks 120` provides a bounded
two-second host run. Hosting and level simulation alone do not prove the full
game handshake, playable multiplayer, or Windows peer compatibility.

The asset-dependent host check exercises loopback discovery, query replies,
transport admission, bind failure and process shutdown:

```sh
python3 tests/native/test_server_host.py out/native-debug/nolf-server \
  out/native-debug/libnolf_object.so out/native-data --logs out/native-host-check
```

The importer reads the source, stages and validates a separate output,
and refuses to overwrite an existing destination. Original files are never
modified and no installer or game executable is run. The English game is
selected by default; `--language russian` imports the image's first optional
Russian localization. The image's alternate translated campaign archive is
not substituted for the original GOTY content.

`assets.json` records archive order, source/output hashes, worlds, music,
and removed executable entries. Imported REZ copies have executable payloads
zeroed. Resource strings, credits, icons and console font data are exported
from PE resources without retaining executable code. The resource catalogue
uses original numeric IDs, UTF-8 text and raw resource payloads.

The supplied image's installer identifies itself as GOTY 1.004. Its original
English archive set is imported for the 1.003 compatibility target; that
installer label alone does not establish save or network compatibility.

## Boundaries and ownership

- `src/platform/` owns OS and SDL interactions. Interfaces expose engine
  responsibilities instead of Windows types. OpenGL context operations are
  separate from the device's drawing implementation.
- Engine/game code must use the platform interfaces. The native boundary
  audit checks sources and project headers reachable from the actual
  compilation database and reports remaining historical OS dependencies
  separately.
- Runtime pointers and worker-message payloads use native pointer widths.
  SDK stream lengths, resource IDs, file fields and network fields remain
  explicitly sized. In-memory structure layout must never define a file format.
- Asset paths normalize legacy separators and ASCII case. Directory
  collisions are errors. Later archive mounts override earlier ones.
- The host owns its SDL window and GL context. Renderer termination detaches
  that context after freeing device objects. The historical Windows renderer
  uses a separate WGL adapter that owns the context it creates.
- Messages implement the SDK's real `ILTMessage` interface. Packet storage,
  string ownership and compression have separate implementations. Reads are
  bounded by written payloads, compound reads rewind only after completion,
  and scalars use explicit little-endian encodings. Native resource formatting
  supports the numbered string/integer inserts found in the supplied game data.
- Resource trees and world geometry have separate runtime types. The file
  manager owns stable 16-bit file identifiers and delivers worker registrations
  on the engine thread. Bounded SDK streams share owned physical file handles,
  retain independent cursors, and survive mount teardown. File copies stage data
  in bounded chunks and replace the destination only after a complete read.
- Worker shutdown is cooperative and joins the worker. Derived workers must
  call `Term` before destroying state used by their callbacks.
- Native object modules expose a versioned descriptor that checks the C++ ABI,
  pointer width, SDK record sizes, interface versions, and an SDK fingerprint
  generated from the shared headers. The loader validates
  class inheritance and properties before constructing aligned game objects.
  Objects and shells retain their module until their destructors finish.
- The class registry owns game instances and per-class profiling records.
  It releases its SDK internal slots on teardown and rejects reloads while
  game instances are alive. Retiring generations detach before game destructors
  run; constructor and destructor callbacks may terminate or delete the registry
  without publishing stale instances. Game class names retain case-sensitive SDK lookup.
- World and statement properties own native strings and typed SDK values.
  Nested creation restores the outer property scope, while world replacement
  discards old scopes. The world loader uses its bounded decoded inventory;
  it no longer sizes runtime properties from original 32-bit offsets. File-cache
  entries use typed storage and snapshot iteration across client sends.
- World transitions own their streams, validate requests before game callbacks,
  retire loaded paused worlds, and check lifetime generations after shell calls.
  Reentrant transitions are rejected and `RunWorld` is idempotent. Tests retire
  or delete the server during random seeding, pre-start, caching and post-start.
- Segment intersections use native polygons and per-query state, including
  transformed planes and surface flags. Object and geometry lifetime tokens
  protect filters that remove a hit, replace a transform, destroy geometry or
  perform nested queries. The nearest surviving candidate supplies the result.
- Engine startup uses an owned catalogue of imported `ltmsg` resources. Resource
  messages format without loading a DLL; catalogue replacement retains readers'
  ownership. Shared console bindings use integer storage of the declared width.
  Native allocation and pool ownership replace process-wide CRT heap overrides.
- Shared record pools and model arenas use native alignment and bounded size
  arithmetic. Engine and SDK counters report microseconds through typed storage.
- UDP and local transports use one typed driver/manager boundary. IPv4 sockets,
  resolution, interface enumeration and bind operations live in the platform
  adapters. UDP discovery and join requests preserve the original wire format;
  repeated joins, capacity rejection, disconnect reasons and driver restart are
  covered by real socket tests. Local transport delivers callbacks on each
  endpoint's owner thread.
- Transport queues use actual sent/received/fragment records. Real manager tests
  cover ordered local messages, fragmented reliable UDP delivery, acknowledgements
  and cleanup. Malformed acknowledgements, groups and fragments are checked
  before state changes, including generated invalid-packet cases. These tests
  do not establish mixed Windows interoperability or complete multiplayer.
- Loader and physics now share native node, polygon, surface and grid records.
  Polygon allocation uses native vertex sizes. Grid loading checks stream bounds,
  allocation sizes, indices and cycles before replacing a live grid; copies own
  independent cells and nodes. Single-cell terrain axes can have zero width
  without dividing by zero. Physics traversals use growing storage because
  original levels contain valid trees deeper than the old fixed stacks.
- The bounded version-66 geometry decoder reads real model, texture-name,
  polygon, portal, terrain and physics-grid tables. It resolves file indices
  into native pointers without overwriting serialized polygon keys, retains
  surface-class properties, and releases incomplete loads. The server loader
  uses this decoder. A separate file decoder owns world metadata, spatial-tree
  layout and all model records. Locally, both GCC and Clang sanitizers decoded
  all 102 imported worlds: 20,183 models, 1,263,680 polygons and 1,094 terrain
  sections. This validates geometry decoding and ownership; world simulation
  and campaign play remain release gates.
- Baked lighting uses one shared record definition and five owned pools. Its
  bounded loader stages replacements and retains existing lighting on failure.
  All 102 imported worlds decode, including 369 animations and 465 frames.
- Point, sphere and line queries use the canonical geometry records. Line
  queries share one typed SDK filter/handle boundary and traverse iteratively;
  component tests exercise 5,000-level trees, rejected polygon filters and
  crossing order. Resource streams buffer scalar reads locally while retaining
  independent cursors and bounded archive extents.
- The server owns one world state for geometry, object definitions, lighting,
  spatial layout and the retained SDK stream. The former eight-byte world
  placeholder and overlapping manager storage are gone. Inherited worlds own
  independent mutable geometry; failed data loads preserve the live state.
  Native state loading passes for all 102 original levels, including 79,968
  object definitions and 26,552 enabled object lights. This checks world data;
  game object creation and simulation remain unfinished.
- Native spatial queries use the serialized quad-tree layout and actual object
  bounds. Membership detaches on native object destruction. Queries snapshot
  identities so callbacks can remove, destroy, move or reinsert candidates.
- All eleven engine object types inherit the same native `LTObject` definition.
  Their manager owns aligned typed banks, object identities and per-type lists;
  server accessors no longer locate those lists through a historical byte offset.
  Attachments, standing relationships and BSP memberships detach during teardown,
  including when geometry disappears before its objects. Sprite control implements
  the actual SDK interface. Model instances share native model state with the
  loader; complete model loading and animation evaluation remain unfinished.
  Runtime tests cover every factory, 1,000 recycled objects, manager restart,
  instance transforms and poly-grid membership. Asset tests construct objects for
  20,181 model names and 1,094 terrain sections across all 102 levels, preserving
  first-match lookup for two duplicate model names. This exercises engine records;
  the actual game classes and campaign are not running yet.
- The engine hash uses owned native records and stable key storage. It preserves
  original key modes, collision order, duplicate lookup, and filename folding;
  it also handles single-bucket tables and validates sizes before allocation.
  Runtime checks cover the complete mounted asset/resource name catalogue.
  A shared typed record pool owns construction, alignment, destruction and
  recycling. Teardown requested from a record callback waits until that record
  is no longer using its storage. Object-state pool teardown releases retained
  effect messages before returning pages. The undefined legacy bank base and
  overlapping hash declarations have been removed.
- The console owns native variable and command records. Engine and SDK parsing
  share one bounded lexer, and `ConParse` is accessed through its actual type.
  Empty arguments, grouped text, escapes, buffer limits and callback teardown
  have runtime coverage. Config files use the platform file adapter and atomic
  replacement; saved values preserve case, punctuation, controls and empty text.
  Numeric bindings parse independently of locale and avoid undefined integer
  conversions. Command results belong to each console instance. The two original
  config files (762 statements) run under GCC and Clang sanitizers. Game-side
  parser buffer fixes await the complete game-module build.
- Model loading, instances, trackers and skeleton evaluation now share native
  model, node, animation and mesh-piece records. Private model overlays and
  historical size-only collection instantiations are excluded from native
  execution. A bounded ABC preflight checks linked chunks, native allocation
  budgets, skeleton depth, frame ordering, finite transforms and socket indices
  before replacing a model. Native arrays use their actual sizes instead of the
  file's historical arena calculation. Evaluators use local scratch and growing
  ancestor paths; callback and user-data pointers retain native types. Trackers
  handle empty animations, large deltas and callback resets. Allocation-failure
  cleanup, truncated files and 2,000 byte mutations have runtime coverage.
  Both sanitizer compilers load and evaluate all 695 original ABC files: 8,770
  nodes, 2,800 embedded animations and 60,295 keyframes. These corpus tests omit
  external child-model resolution in that independent-file check. The native
  model cache also resolves all 236 external child references and evaluates
  47,267 bound animations under both sanitizers. Parents retain shared child
  ownership while the cache keeps weak entries. Runtime checks cover concurrent
  loading/release, retries after missing or cyclic dependencies, engine file
  identifiers, and models surviving cache/catalogue teardown. Filename cleanup
  uses matching array allocation/deallocation, and filename replacement safely
  reads the previous owned value. The server source now loads and publishes
  through an owned model service. Tickets retain queued and completed results,
  cancellation prevents stale publication, and instances retain shared ownership.
  The owner can poll ready tickets when the worker queue cannot allocate a reply.
  Model retirement clears borrowed hash entries before releasing resources.
  Pending secondary trackers preserve animation identity, elapsed time and key
  callbacks across placeholder replacement. File and skin changes are staged
  until binding succeeds. Both sanitizers exercise all 695 models and 47,267
  bound animations through the service with 695 engine file identifiers.
  The complete native server library now links. Manager startup, shutdown and
  native SDK fixture actors are tested; simulation in original levels and the
  actual game module remain separate runtime gates.
- Shared common and transform APIs now call actual SDK virtual interfaces.
  Attachment socket handles and node handles biased past the socket table both
  work in local and world space. Transform inversion returns the inverse, and
  composition, difference and output aliasing have runtime coverage. Invalid
  hidden-piece indices are rejected before shifting the 32-bit mask.
- Model string keys deliver the actual SDK `MID_MODELSTRINGKEY` and `ArgList`.
  Commands retain their text when a callback releases the model. Independent
  lifetime guards stop animation advancement after object destruction, tracker
  removal, animation reset, model rebinding or deferred object removal. Native
  tracker snapshots tolerate removal of later trackers during a callback.
  First model binding now starts playback. Both sanitizer compilers exercise
  these destructive callbacks and re-run the complete model-service corpus.
- Client file transfer owns filenames and streams through one native type.
  Private transfer/descriptor/vtable views and the unused string-holder base
  are removed from the server path. Actual native network managers exercise
  multi-packet metadata, the six-block acknowledgement window, exact 80 KB data,
  empty files, malformed replies and cancellation. The same 80 KB transfer
  passes over IPv4 UDP with reliable fragmentation and bounded receive polling.
  Remaining byte counts and
  idle state now reach completion; failed reads send cancellation instead of
  transmitting zero-filled error data. Streams release exactly once and can
  outlive their mounted catalogue. This validates the native transfer component;
  mixed Windows peers and full server join/startup are still release gates.
- Native client state owns its packet references, connection data, object tables,
  names, transfer state and ID lists. Client, light-change, attachment, packet-set
  and destructor layout views have been removed from the server path. SDK client
  callbacks use exact opaque-handle signatures and validate live membership before
  dereferencing a handle. Name buffers terminate, command/page indices are bounded,
  view positions reject invalid values, and handshake decoding stages complete data.
  World-entry readiness reads actual transfer counters. Camera attachments reject
  cycles, detach safely and send a real restoration message. Light queues merge
  repeated changes and validate wire widths. Client visits and command dispatch
  survive callbacks that delete current or later clients; retained connection data
  remains valid during world-entry callbacks. Runtime coverage includes these
  lifetimes, all-client object-table growth, packet reference counts and malformed
  handshake prefixes under both sanitizers. Full manager entry/exit and game-shell
  callbacks are compiled and inspected; their end-to-end runtime proof still awaits
  complete server linking and lifecycle consolidation.
- Sound metadata uses one bounded RIFF parser for memory and mounted file
  extents. It validates PCM frames, IMA block/fact capacities and MP3 headers,
  resolves cue/label loops independently of chunk order, and skips opaque
  vendor LIST forms found in the original assets. The native cache owns names,
  file IDs, wanted-pass state and shared immutable metadata. Both sanitizers
  load all 4,799 original WAV files: 887 PCM, 250 IMA ADPCM and 3,662 MP3,
  including 24 authored loops. These checks do not decode or play sample data.
- Server sound instances use constructed native records, typed object IDs and
  shared track ownership. Requests apply SDK defaults only to ignored fields
  and reject invalid used values. Clients own queued sound records and delta
  maps. Independent sound encoders validate exact Windows-format packet
  fixtures and commit state/listener holds only after a complete append.
  Inter-object links own native endpoint records and embedded memberships;
  sound endpoints have their own type. Link retirement precedes notifications,
  and callback iteration tolerates clearing or replacing other records. The
  server now implements the actual SDK sound interface by inheritance. These
  components pass runtime checks. Native audio decoding and device playback
  remain release gates.
- Attachment records now retain actual endpoints and belong to a manager
  index. SDK handle adapters validate ownership before reading a record;
  removing either endpoint retires its memberships. Cycle and invalid-offset
  checks precede publication. Child notification snapshots distinguish record
  generations even when callbacks recycle the same memory. Tests cover
  callbacks removing a parent, current children and later children.
- World visibility owns validated BSP planes, topology, region tables and PVS
  bytes. Queries decode the original bit order, combine open portal entries,
  and filter native object bounds. Nested queries have independent state;
  callback guards tolerate removal of objects, geometry and the spatial index.
  Client interest records distinguish recycled object generations. Both
  sanitizers validate all 102 original worlds: 20,183 models, 70,790 regions
  and 85,153 PVS entries. Container point queries use real native transforms
  and geometry. Whole-server update behavior still awaits runtime validation.
- SDK writable file streams use an owned platform seekable-file adapter.
  Unicode paths, cursor changes, read/write transitions, sticky errors and
  32-bit SDK extents are tested. Packet tracing calls the real stream interface;
  its private vtable view and direct CRT file operations have been removed.
- The version-1113 save codec owns fixed-width values for all 11 object types,
  portal flags, timing words, console values, animation/client state and links.
  It validates complete records, absolute offsets, sentinels and reference
  graphs before publishing a document. Game callback messages share value
  codecs with network messages and reproduce the original per-field guards,
  including individual string and nested-message bytes. Each operation owns
  its object-index table and retains lifetime guards. Independent layout
  fixtures, every truncated prefix and 10,000 mutated files exercise the codec.
  A separately loaded SDK test module reads and writes guarded save messages
  through the actual virtual interface under both sanitizer compilers.
  Live save/restore adapters now use these native records and callback messages;
  the old padded save views and fabricated vtables are removed. Output is staged
  before atomic file replacement. Restore validates the document before changing
  live state, creates the actor reference table before game callbacks, and rolls
  back created actors, timing, portals and console values on failure when the
  original world remains alive. Restored client references own strings and actual
  object lifetime guards. Tests load a native SDK fixture module and round trip
  live engine/game state, attachments, inter-object links, SFX, sky membership
  and pending client references. Failed game-payload restore removes newly
  created actors while retaining existing actors. Engine fields are captured
  before each actor's save callback, and graph links after all callbacks.
  Malformed versions, console rollback and Unicode file replacement also pass.
  Original game actors and Windows-generated saves still need validation.
- Physics uses one native mover interface and request record. Incompatible
  private vtables, padded move/geometry views, the force-world overlay and the
  step-height interface view are removed. Collision scopes restore the previous
  request on exit, and callbacks retain object generations and module ownership.
  Real manager tests cover SDK global force, box collisions and touch callbacks,
  attachment following, removal during activation, removal of either collision
  endpoint, world end and server teardown during a callback. Dynamic collections
  replace the original 32-rider and 128-contact stack buffers; 80 riders and 180
  simultaneous contacts pass under both sanitizer compilers.
- The native server also loads all 102 original levels and performs 1,216 box
  sweeps from authored start points in 101 levels, with and without stair
  stepping. It validates finite blocking faces and 1,212 supporting contacts;
  four sweeps finish airborne after sliding. One level has no qualifying start.
  Both sanitizer compilers pass this corpus. These tests use the native SDK
  fixture module and small box dimensions, rather than original gameplay actors.
  Large moves use bounded collision steps. Cross-cell queries gather unique
  native polygon candidates, and stair corrections preserve a nonnegative foot
  volume. Supporting polygons must touch the final body, including after move
  rollback. Main-world surface queries remain distinct from container/brush
  volume queries. Shared polygon clipping has no historical 32/40-vertex limit;
  scratch scopes restore state after nested or exceptional exits, and missing
  physics data returns a defined position. Focused asset-free regressions cover
  these cases. Player-cylinder collision, moving/rotating brush geometry and
  original game simulation still need validation.
- All native object types share an independent callback lifetime token, and
  model callbacks use that same token. World-data generations catch replacement
  at recycled addresses. Object and container visits tolerate removal during
  callbacks. Server update, movement and removal adapters use these guards;
  their actor simulation and callback runtime proof is still incomplete. Both
  sanitizer compilers re-run the original 102-world object corpus and the
  695-model service corpus after these layout and lifetime changes.
- The owned shade grid bounds allocation and samples safely at level edges,
  including flat one-cell levels. World command parsing preserves quoted and
  parenthesized arguments, percent escapes and case-insensitive lighting keys.
  Position compression uses a typed reference to the original file bounds;
  the grown collision bounds have separate storage. Light-animation handles
  retain their documented 32-bit width on Linux and Windows.
- The configured server sources now build a shared native library with zero
  unresolved symbols. Strict linking is part of the default build under GCC,
  Clang and the Windows cross-compiler. Sanitizer builds retain the same strict
  gate; Clang uses its shared sanitizer runtime. Runtime checks cover construction
  before initialization, startup, shutdown, restart and the public server exports
  with an owned resource catalogue. Helpers, native pool sizes and list heads are
  initialized before the worker starts. End-world uses guarded client/object
  visits, and static-class filtering uses the actual SDK class record. The headless
  application uses the public server exports. Loopback tests cover original-map
  hosting, discovery, GameSpy query replies, application GUID rejection, transport
  admission, bind conflicts and normal SIGINT/SIGTERM cleanup under both sanitizer
  compilers. These peers do not perform the game handshake or enter a player.

## Remaining release gates

1. Consolidate remaining reconstructed update and packet interfaces. Run the
   real game object module and complete original-level collision, simulation,
   actor callbacks and save/restore through the native server.
2. Connect the client, game shell and complete renderer core to the native
   platform services and authoritative runtime types. Validate every level.
3. Listen-test sound and music against the original, finish the underwater
   air-meter font, and complete a campaign playthrough on real hardware.
4. Prove bidirectional Windows save compatibility and mixed-platform LAN play.
5. Complete campaign playthrough, client/server soak tests, hardware/controller
   matrix and asset-free distribution packaging.

Passing the current portability tests is an intermediate milestone. It does
not satisfy these remaining release gates.
