# Sagas Stellar Studio

Run `python3 sagas/tools/stellar/server.py` from the workspace and open
http://127.0.0.1:8765. Restart an existing server after updating its code.

Import an original `stellar_project.json`, its character directory, or a parent
Characters directory. Source FBX conversion uses Blender at
`/Applications/Blender.app/Contents/MacOS/Blender`. No StellarExport project or
Remix overlay build is required. Reimporting updates the package by name.

Drag roster entries into grid cells; holes are preserved in the game. Select
checkboxes and use **Remove selected** to remove individual or multiple roster
slots. Package files remain available in the package list. Save the roster and
reopen character select to apply changes.

Sagas loads converted models in hover previews, selected poses, and battle.
The converter uses Remix's base-specific joint mappings and target-rest-pose
preparation, with two skin influences per vertex and a 512-pixel texture atlas.
Custom portraits receive frame and name labels. Selection WAV audio, gain,
trimming, and inherited fighter pitch overrides are exported to Sagas audio
packages. As in the original tooling, announcer clips do not inherit voice pitch.

The project editor preserves bones, animations, hitboxes, physics, sounds,
stages, and controls. Model settings and audio export are connected to the
runtime; custom animation, hitbox, physics, and stage overrides still need
runtime adapters. The full tool/gameplay port is not complete. The embedded
preview renderer and full 1P campaign are deferred.

Authoring helpers were ported from `remix/game/custom/Toolset`; no generated C,
overlay registry overrides, or broken Remix build integration is executed.
Tests: `python3 -m unittest discover -s sagas/tools/stellar -p 'test_*.py'`.
