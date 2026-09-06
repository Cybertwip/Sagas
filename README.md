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
--title             begin directly at the title scene
--menu              begin directly at the main menu
--select            begin directly at character selection
--battle            begin a Mario/Fox battle on Dream Land
--headless          hidden native graphics window, dummy audio, fixed clock
--frames N          stop after N rendered frames
--capture PNG       save the final frame
--capture-only      skip intermediate drawing during headless captures
```

Enter/Space/A or a controller's south/Start button accepts. Escape/B cancels;
S skips. Startup accepts a skip after the original eight-frame lockout and each
opening segment after its original ten-frame lockout.

In character selection, move the puck with arrows or the left stick and press
A/Space to select. Activate CPU slots by selecting their N/A label, then choose
a fighter for that slot. B recalls the active puck; Enter starts once all enabled
slots have a selection (at least two in VS). Selected previews use the source
submotion flags, including Ness and Pikachu's extra XRotN animation track.

In battle, arrows/stick move, X or upward stick jumps, A/Space performs the first
jab, and Z shields. Controller equivalents are north/west for jump, south for
jab and right shoulder for shield. Escape returns to selection.

Battle currently supports cartridge movement attributes, static stage collision,
gravity, jumps, landing, platform drops, joint-bound first-jab hitboxes, damage,
hitlag, hitstun, knockback and stock respawns. Knockback decays separately from
movement and survives landing. CPU movement uses the same physics/combat path.
This is still a partial gameplay implementation: hurtboxes are body capsules;
the full move set, source action transitions, ledge grabs, moving platforms,
collision responses, sound-event coverage and battle HUD remain unfinished.
Ground knockback currently assumes normal floor friction. Opening fight scripts
have not yet been connected to this simulation.

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
