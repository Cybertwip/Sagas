# Character selection and battle fidelity

Priority from September 6: shared selection and battle systems first; resume opening choreography afterward.

- [x] Load cartridge fighter attributes; preserve held input during catch-up ticks and latch button edges until consumed.
- [x] Decode static stage collision; implement grounded movement, gravity, jumping, landing and platform drops.
- [x] Add joint-bound first-jab hitboxes, damage, hitlag, hitstun and stock loss.
- [x] Separate knockback from movement velocity; preserve horizontal momentum through landing.
- [x] Restore character-selection previews, deposited pucks and configurable CPU slots; pass selected roster into battle.
- [x] Bind selected-pose root tracks from source flags; verify Ness/Pikachu and capture all 12 fighters at frames 0, 30 and 120.
- [x] Add pitch-curve/envelope evaluation to FGM playback and regression tests.
- [x] Port button-jump force, release short hops, character aerial-jump attributes, and Kirby/Purin's per-jump velocities and drift multiplier.
- [x] Bind forward/backward and multi-jump clips; use Ness/Yoshi TransN deltas for aerial-jump physics without applying them twice in rendering.
- [x] Decode jab 1/2/3 hitbox windows and changes, buffer follow-ups using source flags/windows, and end attacks from figatree termination.
- [x] Port default battle-camera interest bounds, fighter zoom/offsets, stage camera bounds, pan/zoom smoothing and camera angles, fitting a 16:9 viewport.
- [ ] Complete aerial turn timing, Yoshi jump armor, multi-jump interrupt windows and rapid-jab transitions.
- [ ] Add camera tracking for projectiles, entry/death modes and stage-specific camera callbacks.
- [x] Add tilts, smashes, dash attacks, five aerials, crouching, multi-hit groups and per-character source motion bindings.
- [x] Add root-motion attacks, grab attachment movement, source damage reactions, knockdowns/get-up choices and 20-tick Z techs.
- [x] Add three-tick smash buffering and verify all three short-hop release ticks across the roster.
- [x] Add Fox special dispatch, source projectile contacts, visible shields and procedural impact/electric/dust effects.
- [x] Add independent controller selection hands/tokens and GAME SET return after the final stock.
- [x] Correct impact pitch timing and prevent hitlag from replaying a motion sound.
- [ ] Complete remaining character-specific special and recovery callbacks and validate Fox travel collision/end behavior against source.
- [ ] Complete source movement/action transitions and throw-specific rotations/statuses.
- [ ] Decode per-joint hurtboxes, moving collision and full source collision responses; floor material friction is implemented.
- [ ] Audit auxiliary joints/root motion and animation event ordering for every battle clip.
- [ ] Complete animation sound-event coverage and verify voice timing/pitch by listening.
- [ ] Complete costumes, CPU settings, disconnect behavior and source battle HUD; basic multiplayer puck interactions and bindings are implemented.
- [ ] Correct shared viewport/scissor handling and verify 16:9 captures.
- [x] Test finite floors, jump startup, gravity/landing, platform drops, hit deduplication, shielding, landing knockback and selected-pose stream bindings; document remaining differences in README.
- [ ] Validate full gameplay behavior against the decomp beyond this tested subset.
- [ ] Reuse the shared battle simulation for the opening's recorded controller scripts.
