# Fighter descriptors

Fighter technical tables are loaded from `<asset-root>/fighters/` at runtime.
The checked-in defaults are in `assets/fighters/`. CMake copies them into
`build/assets/fighters/` when building. Edit the deployed files and restart the
game to tune data without recompiling C++; edit the checked-in defaults to keep
changes across builds. An alternate application asset root also selects its
fighter descriptors.

Each TSV has a version line (`SAGAS-DATA<TAB>1`), a named column header, and
numeric rows. Keep the header, column order, and required row count intact.
Array columns use bracket indices. Blank lines and full-line # comments are
allowed after the header. Floats must be finite; integer overflow, unsigned
negative values, extra/missing fields, wrong schemas, wrong row counts, and
missing files are errors. Diagnostics identify the file and, for invalid rows,
the line. No compiled numeric fallback silently replaces invalid data.

Tables load lazily once per asset root. Set the root before creating scenes;
switching roots invalidates table caches and references into them. This API is
intended for scene initialization on the main thread, not concurrent hot reload.

| Files | Contents |
| --- | --- |
| `fighter_source_data.tsv` | Attributes, movement parameters, animation bindings, capture joints and fighter sound IDs |
| `source_animation_flags.tsv` | Animation transform flags |
| `source_jab_hitboxes.tsv` | All extracted attack hitboxes, damage, knockback, joints, timing and sound IDs (historical filename) |
| `source_motion_flags.tsv`, `source_special_flags.tsv` | Script callback flags and timing |
| `source_hit_status.tsv`, `source_model_parts.tsv` | Invulnerability and model-part changes |
| `source_jab_followups.tsv` | Jab continuation windows |
| `source_throws.tsv`, `thrown_clips.tsv` | Throw damage/release data and victim animations |
| `weapon_source_data.tsv` | Projectile contact attributes |
| `weapon_appearance.tsv` | Projectile asset addresses, render flags, palettes and RDP overrides |
| `special_physics.tsv` | Named recovery, bomb-throw, ground-pound, star and tether tuning |
| `battle_motion_sounds.tsv` | Motion sound events |
| Remaining tables | Callback sound IDs, sleep/cargo motions, Samus charge sizes and sounds |

Fighter rows use the runtime roster order: Luigi, Mario, Donkey, Link, Samus,
Captain, Ness, Yoshi, Kirby, Fox, Pikachu, Purin. Thrown-clip dimensions are
holder, victim, then forward/backward. Weapon rows are Luigi fireball, Mario
fireball, Fox laser, Samus charge shot, Link boomerang, PK Fire spark, Thunder
Jolt, Thunder trail, grounded Jolt, Final Cutter, PK Thunder, PK Fire flame,
Yoshi egg, Samus bomb, Link bomb, Yoshi star.

The corresponding C++ headers retain typed schemas/readers rather than numeric
aggregate initializers. Behavior callbacks and state transitions remain C++;
this is a data system, not a scripting engine. Some callback-specific constants
still remain in the implementation and can be migrated using the same mechanism.
Model geometry, textures and animation bytes remain in the existing asset archive.

## Regeneration

The existing extraction tools now call `export_fighter_descriptors.py` after
extracting US source data. The exporter writes TSV values and replaces generated
aggregate initializers with typed runtime table declarations:

- `extract_fighter_data.py`
- `extract_battle_attacks.py`
- `extract_throws.py`
- `extract_battle_callbacks.py`
- `extract_weapon_data.py`
- `extract_opening_audio.py --battle`

Run their existing commands with the local decomp/build prerequisites. Opening
cinematic audio is separate and unchanged. Regeneration overwrites extracted
defaults; preserve intentional tuning edits first. `special_physics.tsv` is
maintained directly, with US callback values from the fighter-specific headers
and `wpvars.h`; its bomb-throw parameters describe the runtime's simplified item
throw behavior. The runtime does not yet implement every source item-throw state
or the source particle-script interpreter.

To add a table, declare a numeric struct and its `descriptor_read` overload,
then a `DescriptorTable<T>{filename, schema, expected_rows}`. A schema change
requires updating both the typed reader and the deployed header/data together.

## Validation

`ctest --test-dir build --output-on-failure` includes parser rejection tests,
an actual Mario gravity edit through an alternate asset root, cache invalidation,
and the existing physics and battle regressions. Native battle captures cover
Yoshi's startup lift and landing stars, Link's airborne left/right bomb throws
and explosions, and Samus's grapple rendering.
