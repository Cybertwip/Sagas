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
--headless          use SDL's dummy platform drivers
--frames N          stop after N rendered frames
```

Enter/Space/A or a controller's south/Start button accepts. Escape/B cancels;
S skips. Startup accepts a skip after the original eight-frame lockout and each
opening segment after its original ten-frame lockout.

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

The opening state follows all 19 original segments and their 3,650-tick
timeline. Decoded sprite/wallpaper scenes use original PNG exports. Shots whose
only source is an N64 display list currently retain their exact timeline and
use a composed portrait/wallpaper presentation until the display-list and
skeleton decoder is connected.

