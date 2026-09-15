# Smash Remix native port

The first port slice translates local `smashremix/` data into the Sagas
descriptor system. It does not run MIPS code, emulate the patched ROM, or mark
new fighters as complete merely because they inherit a base fighter.

Run from the workspace root:

```sh
python3 sagas/tools/import_smashremix.py
cmake --build sagas/build -j 4
ctest --test-dir sagas/build --output-on-failure
```

The importer accepts `--source` and `--output`. It requires no network, ROM
execution, assembler or Python packages outside the standard library.

## Current translated data

- `remix_roster.tsv`: stable source IDs, roster keys, native parent IDs,
  attribute offsets, nine file bindings, additional action count, jab and
  inhale-copy declarations. This includes RANDOM and polygon/regional variants.
- `remix_actions.tsv`: resolved numeric action/animation/flag overrides and
  links to imported moveset scripts. `-1` means inherited; script `-2` means an
  unresolved script expression. These are candidates requiring source-order
  and conditional-patch validation, not a blindly applied runtime patch.
- `remix_scripts.tsv`: whether a binary is fully self-contained and decoded.
- `remix_hitboxes.tsv`: timed hitbox windows, joints, signed offsets, damage,
  knockback, element, ground/air mask, sound fields and shield damage.
- `remix_events.tsv`: timed source commands and their words, preserving sound,
  flag, presentation and other event data for native event dispatch.
- `remix_import_report.json`: source locations, unresolved expressions,
  executable callback declarations, required resources and script hashes.

`RemixDescriptors.hpp` exposes typed native readers.
`remix_hitboxes_at(script, frame)` rejects scripts that need linking rather
than exposing incomplete hitboxes. The native integration test translates
Falco's jab, checks frames 3–5 and four damage, and feeds the resulting hitbox
into Sagas's existing combat resolver. Falco is selectable on the CSS through
`remix_css.tsv`; he uses Fox parent callbacks with Falco's model and attributes.

The decoder handles bounded loops and hitbox creation, clearing, mutation,
signed fields and timing. Unknown extended opcodes, malformed data, pointers,
subroutines, jumps and ASM continuations reject the entire script translation.
Preserving a timed event is not the same as implementing its runtime behavior.

## Remaining port work

1. Link ASM moveset labels, inserted binary continuations and subroutines;
   resolve conditional action edits in assembly inclusion order.
2. Translate remaining fighters' executable movement, interrupts, collision,
   weapon, capture and copy behavior into native state handlers. Falco is the
   first Remix character on the dynamic CSS: Fox parent behavior, Falco model
   and attributes, selected through `remix_css.tsv` / `remix:FALCO`.
3. Extend native fighter identity beyond parent behavior kinds, with per-action
   callbacks and CSS portraits that are not inherited from the parent.
4. Connect timed event dispatch for imported scripts and verify each fighter
   before enabling it on the CSS.
5. Port stage geometry/hazards, items, music routing, menu/game modes and Remix
   engine-wide mechanics through the same data/callback boundary.

The initial report contains 69 roster declarations, 6,642 resolved action
declarations, 663 decoded scripts, 1,441 hitbox windows and 9,399 timed events.
It also identifies 1,612 callback declarations and 248 unresolved action
declarations. These are source import metrics, not playable-character counts.

Enabled Remix characters inherit their parent's native callbacks until those
callbacks are ported. They do not substitute a missing model or archive file.


## Recovered 2.0.1 resources

The supplied `data/smashremix2.0.1/patches/smashremix2.0.1.xdelta` was applied
to the local US base ROM. The output MD5
`2b2d6b295106c54216b7fc7a2f14346e` matches the release README exactly.

`tools/extract_smashremix_assets.py` produces an isolated asset root at
`assets/remix/`: 5,455 decompressed resources, relocation links, SHA-256
per-file manifest, and archive provenance. Cloned MAIN files that kept parent
graphic offsets but listed a smaller info file are retargeted onto the parent
graphics; remaining overflow chain nodes are repaired the same way. Re-run
`extract_smashremix_assets.py --repair` on an existing bundle after edits.
The original 14 out-of-range references are gone. Resource 5439 has a one-byte tail beyond its table's word count; the
verified VPK byte length is used. This bundle contains the resource archive,
not a complete replacement for the default Sagas asset root.

For an already reconstructed ROM:

```sh
python3 sagas/tools/extract_smashremix_assets.py
python3 sagas/tools/import_smashremix.py --resources sagas/assets/remix
```

To reconstruct as well, pass `--patch <xdelta-path>` and optionally
`--base-rom <base-path>` and `--rom <new-output-path>`; xdelta3 must be installed.
An existing ROM output is never overwritten. Extraction into a populated asset
folder requires `--overwrite`; use a separate `--output` to compare a fresh
extraction without losing edits. The source ROM and patch remain
untouched. The reconstructed ROM stays at `build/remix/smashremix.z64` in the ignored build tree.
Extracted resources live in-tree at `assets/remix/` for editing. CMake deploys
them to `build/remix/assets/` on each build; the native archive test uses that
deployed path. Edit the in-tree files, then rebuild.

The descriptor importer auto-detects the in-tree `assets/remix/` extraction when
`--resources` is omitted. Its report now has zero missing roster file
references. This removes the resource-availability blocker; it does not mark
ASM callbacks or new fighters as fully ported.
