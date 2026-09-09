# Sagas Studio / Stellar port

Run `python3 sagas/tools/stellar/server.py` from the workspace, then open
http://127.0.0.1:8765. The server only binds to localhost. Use `--assets` to edit
another Sagas asset bundle.

Import a character's Stellar JSON, FBX/GLB/GLTF model, PNG portrait and WAV files.
Arrange the roster by dragging entries, hide entries, set its column count,
and save. Reopening character select loads `build/assets/mods/roster.tsv` and
fits its portraits into the selection area. Original gameplay remains Sagas.

This begins the tool port; it is not the completed custom fighter runtime.
Imported models, animations and sounds are preserved in portable character
packages. Until model conversion and runtime character definitions are ported,
imported entries explicitly use the selected original fighter as their base.
No generated C, `game_overlay` directories, registry overrides, or broken Remix
build integration are imported or executed.

The `stellar_project`, `stellar_shared`, `stellar_audio`, `stellar_validation`
and `stellar_batch` modules were copied unchanged from
`remix/game/custom/Toolset`. They retain the existing project schema and reusable
authoring services. `server.py` and `index.html` are the Sagas editor/integration.

Next: adapt Blender conversion to Sagas model assets; load character attributes,
motions, hitboxes and audio by package ID; pass package identities through CSS
and battle; add package validation and an animation/combat preview. Port
Smash Remix gameplay and intro systems separately against the working Sagas
engine, retaining the decomp as the physics/state reference.
