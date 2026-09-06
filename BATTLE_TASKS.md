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
- [ ] Complete source movement/action transitions, aerial attacks, specials, grabs and ledge grabs.
- [ ] Decode per-joint hurtboxes, moving collision, material friction and full source collision responses.
- [ ] Audit auxiliary joints/root motion and animation event ordering for every battle clip.
- [ ] Complete animation sound-event coverage and verify voice timing/pitch by listening.
- [ ] Complete CSS puck interactions, costumes, CPU settings, multiplayer controls and source battle HUD.
- [ ] Correct shared viewport/scissor handling and verify 16:9 captures.
- [x] Test finite floors, jump startup, gravity/landing, platform drops, hit deduplication, shielding, landing knockback and selected-pose stream bindings; document remaining differences in README.
- [ ] Validate full gameplay behavior against the decomp beyond this tested subset.
- [ ] Reuse the shared battle simulation for the opening's recorded controller scripts.
