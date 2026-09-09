# Sagas

Sagas is a compact C++20 reimplementation of the Smash Remix runtime. It reads
external, unpacked game data; no relocation or audio payload is compiled into
the executable.

## Build and run

```sh
cmake -S sagas -B sagas/build -DCMAKE_BUILD_TYPE=Release
cmake --build sagas/build -j
./sagas/build/sagas
```

The normal build creates `sagas/build/assets` and validates every decompressed
relocation against `relocData.csv`. Override the source and output locations
with `SAGAS_REMIX_ROOT` and `SAGAS_ASSET_ROOT` at configure time.

Runtime options:

```text
--assets PATH       use another external asset bundle
--controls PATH     load/save controller bindings (default controls.cfg)
--title             begin directly at the title scene
--menu              begin directly at the main menu
--select            begin directly at character selection
--battle            begin a Mario/Fox battle on Dream Land
--headless          hidden native graphics window, dummy audio, fixed clock
--frames N          stop after N rendered frames
--capture PNG       save the final frame
--capture-only      skip intermediate drawing during headless captures
```

Space/J or controller south confirms menus; Enter/Start begins a match.
Escape/B cancels. S skips the opening, but does not start a match from selection.
Physical A is movement, never menu confirmation.

Character selection supports mouse pickup/drop and independent hands/tokens for
up to four connected controllers. Tokens stay inside the portrait grid while
hands can visit Back and slot settings. Click an N/A slot label to add a CPU.
Back sits at the upper right; inactive slots use the original closed shutters.

The default controls follow `remix/src/input.c`; F2 opens the binding editor.
Bindings and tap-jump preference persist in `controls.cfg`.

| Action | Keyboard | Controller |
| --- | --- | --- |
| Move | WASD | Left stick / D-pad |
| A attack | J | South |
| B special | K | East |
| Jump | I | North, West, or right stick |
| Shield / Z tech | Shift, U, or Z | Either trigger |
| Grab | E, or shield + attack | Right shoulder, or shield + South |
| Taunt | Q | Left shoulder |

Directional attacks include tilts, smashes, dash attacks and five aerials. A
fresh strong directional tap selects a smash; held directions select tilts.
Smashes have a three-tick input queue, paused during hitlag. Releasing jump
within the first three startup ticks gives a short hop. Down crouches; a fresh
down tap drops through pass-through platforms. After knockdown, attack gives a
get-up attack, left/right rolls, and up or shield stands up. A shield press less
than 20 ticks before tumble impact performs a neutral/directional tech.

Battle uses cartridge movement attributes, floor friction, animation root
motion, joint-bound attack windows, multi-hit collision groups, hitlag,
knockback, grounded/airborne damage reactions, tumble, knockdowns and get-ups.
Grabbed fighters follow the holder's animated attachment joint through throws.
Hit, electric, landing and launch effects, visible shields, and a GAME SET
transition replace the previous silent/frozen match end. Audio events are
consumed once per motion frame; impact articulations retain timed pitch changes.

Special motion dispatch and basic projectile contact are available. Fox has
Blaster, aimed Firefox startup/travel/end, and held Reflector with projectile
reflection. **This remains a partial port, not verified 1:1 gameplay.** Remaining
work includes complete character-specific special callbacks (charging, capture,
weapon behavior and recovery), exact thrown rotations/status tables, per-joint
hurtboxes, moving collision, full source collision responses, CPU strategy,
source particle assets and complete sound-event coverage. Most effects remain procedural; Pikachu projectile models now load source geometry and materials. Full source effects and visual parity across
every opening segment have not been established. Opening fight scripts are not
yet connected to the shared battle simulation.

The default battle camera follows `gmCameraUpdateInterests` and
`gmCameraDefaultFuncCamera`: fighter offsets, facing margins, stage bounds,
distance fitting, yaw/pitch and pan/zoom smoothing. Viewport fitting uses 16:9.
Projectile, entry/death and stage-specific camera modes remain outstanding.

Regression checks:

```sh
ctest --test-dir sagas/build --output-on-failure
PYTHONDONTWRITEBYTECODE=1 python3 sagas/tools/view_sequence.py --self-test
# Requires a native graphics session; captures all 12 selected fighters.
./sagas/build/sagas_selection_capture /tmp/sagas-selected
```

`tools/extract_fighter_data.py` regenerates the attributes, selected-motion flags
and motion IDs from the local US decomp build and Sagas reloc manifest.

## Animation sequence viewer

`OpeningScene.cpp` plays the opening animation graph. To inspect the same
models, materials, and N64 textures without running the C++ runtime:

```sh
python3 -m pip install -r sagas/tools/sequence_viewer/requirements.txt
python3 sagas/tools/view_sequence.py
python3 sagas/tools/view_sequence.py --self-test
python3 sagas/tools/view_sequence.py --dump-model llMarioModelJointTreeDObjDesc
```

The viewer reads `sagas/assets/scenes/opening.*.tsv` and the unpacked reloc
bundle. Texture decode follows Smash Remix (`remix/src/texture_decode.c` plus
MObj palettes from `ssb-decomp-re` `objdisplay.c`), not the deprecated
`original_runtime` RDP path. The material dock flags missing palettes, unmatched
formats, and per-joint MObjSub bindings.

## Architecture

- `Application` is the façade for SDL lifecycle and the fixed 60 Hz loop.
- `SceneMachine` and `Scene` implement the State pattern for Startup, Opening,
  and Title.
- `AssetRepository` is a lazy, cached Repository over normalized disk assets.
- Rendering/audio are narrow engine services, leaving their SDL backends
  replaceable as Strategies.
- `AnimationClip` supplies data-driven interpolation and `PhysicsWorld`
  provides deterministic fixed-step integration.

The opening state schedules all 19 segments through source tick 4195. Geometry,
skeletal clips and sprites are decoded from the original assets. Battle-driven
choreography still uses partial scripted approximations; it is not yet a complete
reproduction of the original opening.


## Opening room regression checks

The room uses Master Hand's heavy-item joint (joint 5, descriptor node 1),
releases Mario at tic 380, creates the second dropped fighter at tic 695,
and carries the landed position into the stand animation at tic 1140.
Falling follows the figatree TransN translation deltas; there is no synthetic
gravity curve or fixed fall-distance cap. The `FTAnimDesc` bitfields are the
reference for wrapper roles: the `TRANSN`/`XROTN` macro names in `ftdef.h` are
reversed relative to those bitfields.

Fighter materials evaluate costume 0 before display-list decoding, including
palette selection. Shade-only parts do not inherit a previous primitive color.
The Python sequence viewer uses the same costume, intensity-alpha, and root-motion rules.

Cartridge geometry now carries its one/two-cycle RDP color combiner, primitive
and environment colors, alpha comparison, culling, and texture-generation state
into the native renderer. It uses interpolated vertex lighting rather than the
engine's generic material highlights. Room haze and sunlight use their authored
texture alpha. GPU texture entries retain ownership of their decoded image and
are retired after scene resources release it, preventing stale textures when
allocator addresses are reused.

The eight fighter introductions use their source name cards, stance animations,
posed-camera programs, split viewports, stage models, map spawn points, and
motion-camera endpoints. Their motion panels use cartridge action clips, but
full gameplay status/physics playback and effects remain outstanding. See
[OPENING_TASKS.md](OPENING_TASKS.md) for the remaining fidelity work.

```sh
cmake --build sagas/build -j
ctest --test-dir sagas/build --output-on-failure
python3 sagas/tools/view_sequence.py --self-test
./sagas/build/sagas --headless --frames 453 --capture /tmp/sagas-drop.png
```

C++ test assertions remain enabled in Release builds. The tests cover the
1,949.5-unit Mario drop, stable landed position, Link's spawn position, the red
costume shirt, blue overalls, white shade-only gloves, sunlight alpha, all eight
stage models, and stance-table alignment. Render captures
also check the attachment, landing, spotlight, and revival. These checks do
not establish pixel-for-pixel parity for the entire opening; the renderer still
approximates N64 lighting/compositing, and later segments retain placeholder
presentations.


Sagas Studio begins the Stellar authoring-tool port. Run
`python3 sagas/tools/stellar/server.py` and open `http://127.0.0.1:8765`.
Import packages, reorder/hide roster entries, choose columns, and save; reopen
character select to load its responsive portrait grid. See
[the tool README](tools/stellar/README.md) for the current boundary: custom
model/audio assets are preserved, while imported entries still use a base
fighter until the Sagas package runtime is implemented. No Remix overlays run.

Guard now uses source entry/release and roll motions. Landing recovery prevents
Z-cancel taps from immediately becoming guard. Shield entry/release and impacts
play audio; shield break remains unimplemented. Jump-start attacks can queue an
aerial, Reflector can yield to an available jump, and up-smash accepts run-brake
and jump-start states. Neutral special landing transitions preserve action
frames and projectile state. Special events are keyed by source state, since
several states share animation files but have different command scripts.
Pikachu has timed Quick Attack bursts and Thunder discharge transitions;
Falcon Dive uses capture/release states. Collision and effects fidelity still
require further source porting and playtesting.
