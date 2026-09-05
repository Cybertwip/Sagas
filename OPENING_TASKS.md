# Opening fidelity work

- [x] Use the source holding joint and animation-driven drop/landing.
- [x] Apply default fighter costumes and preserve shade-only part colors.
- [x] Decode room intensity alpha, RDP combiners, alpha comparison, and opaque/translucent display-list selection.
- [x] Use vertex lighting, generated reflection coordinates, and native culling for cartridge models.
- [x] Prevent texture-cache address reuse across scene unloads; keep GPU entries tied to their source images.
- [x] Use the eight source introduction stance clips without an extra root-table offset.
- [x] Use source name cards, posed viewports, camera programs, stage geometry, and MoviePlayer1 spawn points.
- [ ] Reproduce the introduction motion panels with full fighter status/physics playback, grabs, and projectiles. Current panels select cartridge clips from the recorded input timings; this is not the original gameplay simulation.
- [ ] Audit the remaining opening segments and replace placeholder presentations (run, jungle, clash, newcomers, and missing cast/effects in other segments).
- [ ] Establish frame-by-frame parity against original-game captures; current rendered spot checks and smoke tests do not establish 1:1 parity.

Validation: C++ assertions cover root-motion landing, costumes, intensity alpha,
all eight stage decodes, and stance-table alignment. The Python viewer self-test
checks decoding and root motion; its preview shader does not yet implement the
native renderer's complete RDP combiner path.
