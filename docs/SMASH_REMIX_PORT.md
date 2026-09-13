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
into Sagas's existing combat resolver. This is the first tested data-to-native-
combat path for the port; it does not yet enable Falco in character select.

The decoder handles bounded loops and hitbox creation, clearing, mutation,
signed fields and timing. Unknown extended opcodes, malformed data, pointers,
subroutines, jumps and ASM continuations reject the entire script translation.
Preserving a timed event is not the same as implementing its runtime behavior.

## Remaining port work

1. Link ASM moveset labels, inserted binary continuations and subroutines;
   resolve conditional action edits in assembly inclusion order.
2. Supply and import the fighter archive resources referenced by
   `build/master.csv`. This checkout lacks the required
   `smashremix/build/original/*.bin` files, including Falco's 08AB/08AC files.
   The report lists missing references per fighter.
3. Extend native fighter identity and selection beyond the twelve base behavior
   kinds, with descriptor inheritance, asset bindings and per-action callbacks.
4. Translate each fighter's executable movement, interrupts, collision, weapon,
   capture and copy behavior into native state handlers; connect timed event
   dispatch and verify each fighter before enabling it.
5. Port stage geometry/hazards, items, music routing, menu/game modes and Remix
   engine-wide mechanics through the same data/callback boundary.

The initial report contains 69 roster declarations, 6,642 resolved action
declarations, 663 decoded scripts, 1,441 hitbox windows and 9,399 timed events.
It also identifies 1,612 callback declarations and 248 unresolved action
declarations. These are source import metrics, not playable-character counts.

No new fighter currently bypasses missing assets or unported behavior by
silently reusing an original fighter's implementation.
