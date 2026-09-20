# Golden transcripts (legacy engine)

Ordered state transcripts captured from the **old** `match::MatchInstance`
engine before the card-engine ECS rewrite removed it. They are the
frozen reference outcomes the data-driven engine reproduces in the integration
suite.

## Provenance

Captured by the since-deleted `tests/unit/golden_capture_test.cpp` against the
legacy engine: `match::MatchInstance`, its effect queue, and the six rule mods
under the since-deleted `src/match/rules/`. The capture harness was removed
with the legacy engine, so these `.json` files are now immutable
reference data. They describe *legacy-engine* behaviour, not desired
behaviour. Where the new design intentionally changes a mechanic, the scenario
is marked `may_differ` (see below).

## Shape

Each `<scenario>.json` is:

```json
{
  "scenario": "standard_baseline",
  "engine": "old",
  "may_differ": false,
  "steps": [
    { "action": "load", "state": { "...": "ExportState()" } },
    { "action": "draw:Alice", "state": { "...": "ExportState()" } }
  ]
}
```

- `action` is a human-readable label for the scripted API call that produced
  the following `state`.
- `state` is `MatchInstance::ExportState()` captured verbatim after that call.
  `ExportState` carries no wall-clock field, unlike `SerializeBaseState`, so
  transcripts are byte-deterministic.
- A step bundles any `Tick()` needed to settle the effect queue; the recorded
  state is post-resolution. Prompt/input scenarios deliberately stop with the
  engine suspended (`pending_player` / `pending_action` set) and resume on the
  next step.

## Determinism

Every scenario reloads a hand-authored `saved_state` JSON through the reload
constructor (`no shuffle, no deal`), so hands, piles, turn index and direction
are fixed literally. No scenario depends on `Rng().seed()` or on wall-clock
time. Do not add `Start()`-based scenarios without seeding the RNG explicitly.

## Scenarios

| Scenario | Mod | Covers |
| --- | --- | --- |
| `standard_baseline` | — | draw, legal play, turn advance (2p) |
| `seven_zero_swap` | seven_zero | play 7 → choose target → hand swap |
| `seven_zero_zero_rotate` | seven_zero | play 0 → all hands rotate (3p) |
| `draw_stacking` | draw_stacking | +2 stacked, then debt resolved |
| `progressive` | progressive | draw until playable, then keep |
| `force_play` | force_play | drawn playable card auto-played |
| `jump_in` | jump_in | out-of-turn identical card steals turn |
| `no_bluffing_allowed` | no_bluffing | +4 legal (no matching colour) |
| `no_bluffing_denied` | no_bluffing | +4 rejected (holds active colour) |
| `prompt_choose_color` | — | Jolly → choose colour prompt |
| `bot_turn` | — | `TakeBotTurn()` plays best legal card |

## `may_differ`

`draw_stacking` is `may_differ: true`. Spec 12.4 rebuilds draw stacking on
response windows, so its transcript is expected to diverge by design. The
replay consumer tolerates it by skipping the scenario entirely.

## Consumption

`tests/unit/engine_golden_replay_test.cpp` is the live consumer. It
reconstructs each `may_differ:false` scenario on the new `match::engine` and
asserts the fixture's outcome facts (current player, direction, active type,
per-player hand counts, draw/discard pile sizes, winner, placements).
Byte-equality with the legacy `ExportState()` is explicitly not the target. `draw_stacking` (`may_differ: true`) is not replayed.

Regeneration is no longer possible: the capture harness and the legacy engine
it drove are both gone. Do not hand-edit these fixtures.
