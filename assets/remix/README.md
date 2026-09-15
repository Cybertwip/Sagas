# Editable Smash Remix assets

This is the source bundle for the native Remix port. Edit resources here.
CMake copies this directory to `build/remix/assets/` when building Sagas.
Edits made only in the build copy will be overwritten by the next build.

- `reloc/NNNN.bin`: decompressed resource files; filenames use decimal IDs.
- `reloc/NNNN.links.tsv`: internal and external relocation bindings.
- `reloc/manifest.tsv`: resource names, original sizes and extraction hashes.
- `extraction.json`: provenance of the verified Remix 2.0.1 ROM.
- `relocation_diagnostics.json`: remaining out-of-range references after repair
  (empty for the verified 2.0.1 bundle). `--repair` retargets cloned MAIN
  pointers onto parent graphics and drops false reloc-chain continuations.

Manifest hashes describe the original extraction; editing a resource can change
its current hash. Keep relocation offsets consistent when changing binary layouts.

Fighter/move descriptors live separately in `assets/fighters/`. The descriptor
importer automatically uses this bundle to check resource availability.

The reconstructed ROM stays outside this tree at `build/remix/smashremix.z64`.
Re-extraction requires `--overwrite` for a populated directory. Prefer a separate
`--output` directory when comparing fresh resources with your edits.
