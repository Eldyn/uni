# Changelog

All notable changes to UNI are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/). The
`VERSION` file at the repo root is the single source of truth for the current
version; each release below corresponds to a `vX.Y.Z` git tag.

## [Unreleased]

### Added

- **Mod graphics are now looping audio (`audio.loop`)** — storeAudio gets a loop channel; an active presentation's loop plays on its composition target and stops when the affliction expires or the match tears down, and it honors SFX volume and mute.
- Creative P1: Creative flag forces ranked off. Host director command gives cards and edits hands with clear deny logs.
- **Match tension HUD, central fuse line, and creeping vignette** — mounted `FuseLine` in `GameScreen` with centered symmetric drain (`scaleX` toward 50% center at 0) and urgency visibility guard (hidden until <= 25% remaining); dynamic creeping debt vignette in `GameBoard` that darkens, expands inward, and accelerates pulse as deadline runs out; victim danger aura in seat rings; damage vignette flash and camera knockback punch on card draw.
- **Play log parity table runs the shipped bindings** (test) — the parity table now compares the shipped `uni` play log bindings against the frozen fixtures: every row reads the same binding file names from `mods/uni/presentations/` and from `tests/fixtures/play_log_parity/`, and the line keys and the params must match. The core `buildLogLines` side of the table is gone, and the rule-id row check gives way to a guard that every wire event a row uses has a binding in the shipped registry. The table also covers what the fixtures never held: an auto-played wild posts its wild reaction line with the run grouping, and the `uni:own_log` negations hold against the shipped files with a cause chain in memory. Testing only, no runtime change.
- **Card-rule play log bindings shipped as uni bindings** — the `uni` mod now owns the five card-rule play log bindings (play, wild play, auto-play, auto-played wild, wild colour change) as binding files under `mods/uni/presentations/`, with the identifiers `uni:play_log`, `uni:play_wild_log`, `uni:auto_play_log`, `uni:auto_play_wild_log` and `uni:wild_log`. A play or auto-play that carries the viewer's own `uni:own_log` tag posts no line; a wild colour change that an own-log play caused posts neither the play line nor the wild reaction, with the negated `caused_by` tests walking the cause chain. The wild binding gains three cases for an auto-played wild with the same `uni:wild_run` grouping and reaction lines. Only the `defs` and `match_start` content digests in the event stream goldens move, and the game events themselves do not change.
- **Core play log rules shipped as uni bindings** — the `uni` mod now owns the nine event-shaped play log rules (skip, reverse, draw, blocked, reshuffle, keep, near-win, elimination, win) as binding files under `mods/uni/presentations/`, with the identifiers `uni:skip_log` to `uni:win_log`. A skip, a draw or a near-win that one of the viewer's own `uni:own_log` plays caused posts no line: every case of those three bindings ends with a negated `caused_by` test that walks the cause chain for an own-log play. The loader, the schema and the client accept the files unchanged; only the `defs` and `match_start` content digests in the event stream goldens move, and the game events themselves do not change.
- **Bindings: ancestor mode for caused_by** — a `caused_by` test can set `uni:ancestor: true` to walk the chain of causes instead of reading only the direct cause, up to `uni:depth` hops (default 8, the direct cause is hop 1). A hop the client no longer remembers stops the walk. The first `caused_by` test of a case that is not negated supplies the match, and a log param with `uni:cause` reads the fields of the matched event, so a mod line can name the play at the top of a chain of mod events even when another mod emitted an event in between. Client, schema, loader docs and validator updated; no wire change.
- **Draw pile tap accepts the draw debt** — while a draw-stacking window is open and the local player is the debt victim, the top card of the draw pile pulses the same white rim as the 7-0 target highlight, and one tap on the pile sends the window pass response (the same message the fuse-line button sends). The victim no longer has to wait for the window to close. The offer follows the window's hold: it appears at once when the group has no hold and after the hold when a jump-in group is merged. A spectator or a responder without the debt sees nothing. The fuse-line button stays as the keyboard-reachable path. Frontend only, no wire change.
- **Bindings: caused_by test** — a binding test `caused_by` holds when the event that caused this one has the id in `uni:value`, and the tag in `uni:tag` when given. For a core event the cause is the root `card_played` of the entry point (the first one of the chain, not the direct parent), so a `cards_drawn` after a +4 is `caused_by card_played`, never `caused_by active_type_changed`. For a mod event the cause is the previous mod event that the same entry point admitted; the first one has the root `card_played`. Client, loader, validator and schema updated. A `uni:tag` on any other binding test is ignored with the warning `binding.test.shape`, not rejected.
- **Bindings: actor test source** — test `exists`, `equals` or `in` can set `uni:actor: true` to read the envelope actor. Client, loader, validator schema updated.
- **Bindings: cause source in a log param** — a log param with a `uni:field` source can now set `uni:cause: true`. The client then reads the field from the cause event, so a line can say "Ann played Sword and cut Eldyn" for any card or mod. The case needs a `caused_by` test, and the validator checks the field against the event of that test. The loader, the validator, the schema and the client are updated. Until the uni play line moves to bindings, the core play line and the core follow-up lines still post next to a mod line, so a mod gets extra lines until then.
- **Bindings: card part in a log param** — a log param with a `uni:field` source can now set `uni:card_part` to `kind` or `color`. The client decodes the card integer in that field through the loaded card defs, so mods can show the kind id or the color of a card. The validator and the schema are updated.
- **Bindings: log param default** — a log param can now set `uni:default` (string or number). The client uses it when the param source gives no value. A string default is escaped. The validator and the schema are updated.
- **Play log parity proof** (test) — a table test runs the core log rules and one fixture binding per rule on the same events, and compares the line keys and params. Rows that bindings cannot express are listed in the parity gaps file.
- **Keep-drawing line as mod data** — the `progressive` mod now owns the keep-drawing play log line. It has `texts/en.json` and a binding on `progressive:drawn_until_playable` with two texts.
- **Jump-in line as mod data** — the `jump_in` mod now owns the jump-in play log line. It has `texts/en.json` and a `window_responded` binding that logs the seat that wins the jump-in window.
- **Hand swap lines as mod data** — the `seven_zero` mod now owns its play log lines for a swap, a forward or backward pass, and a full redistribution of hands. It has `texts/en.json` and three bindings.
- **Stack line as mod data** — the `draw_stacking` mod now owns the stack penalty play log line. It has `texts/en.json` and a `status_applied` binding with three texts, a heavy one from a total of 8. The line shows the new total and not the last card. A test for each shipped mod checks that its bindings load with no error and no warning.
- **Progressive mod event** — the `progressive` mod now emits the mod event `progressive:drawn_until_playable` (payload `target`, audience `all`) when its draw loop ends, so a binding can write a play log line. A first draw that is playable emits nothing.
- **Bindings: numbers, streaks, first match** — a binding test can now compare numbers with `gt`, `gte`, `lt` and `lte`. A test or a log param can read a server streak value with `uni:streak` and `uni:key` or `uni:key_field`. When the value is missing, a test is false and a param is left out. String values are escaped. A binding can set `uni:select` to `"first"` to use only the first case that matches. The client event now carries the envelope context. The validator rules and the schema are updated.
- **Event contract acceptance sweep** — every acceptance item of the event contract spec now has named tests, listed in `acceptance-matrix.md`. New tests: one combined no-leak scan (three seeds, two mod sets, three bot seats with one private, a spectator, a late seat, a late spectator, snapshot requests for every recipient, a mod event on every play, tail included), a full-system smoke match with the shipped mods and the real validator (no gap in any sequence, `match_ended` once per recipient, no error log from the budgets or the validator), a seat rejoin test (a disconnected human gets the own seat back, and nobody takes a seat that a bot holds), and a client test that a replayed `window_opened` or `turn_advance` does not arm or restart a timer. The six open items of the spec are answered, and the deferred work is listed in `follow-ups.md`. Ticket 25 of the event contract.
- **Old event names removed (contract step)** — the engine no longer queues a second copy of an event under a new name (`kEventAliases` is gone), and the old packets `window_open`, `window_response`, `window_close`, `prompt_open`, `prompt_close`, `roll_result`, `outcome_result`, `placement` and `match_end` are no longer sent. The new names `window_opened`, `window_responded`, `window_closed`, `prompt_opened`, `prompt_answered`, `outcome_rolled`, `placement_set` and `match_ended` are the only ones. The envelope no longer carries `seq` and `type`: `sequence`, `id` and `payload` are required, and a namespaced `id` marks a mod event (there is no separate `mod_event` type). `cards_drawn` and `play_rejected` lost the old `player` field (payload version 2), and the snapshot `window.responders` is now `targets`. Packet volume drops, because the doubled windows, prompts, rolls, placements and match end are single again. The client reads `sequence`, `id` and `target` only, and the guess paths for skip, reverse, keep, wild color and game end (kept as fallbacks since tickets 05 to 09) are deleted. A golden of the new-only stream for three seeded four-bot matches (per seat and spectator) proves that only the old names went. Two guard tests (C++ and vitest) fail if an old event name or `seq` returns. Mod hook names such as `window_open` and the analytics event name `match_end` stay. Ticket 24 of the event contract.
- **Play log and reaction texts moved into the `uni` mod** — the 52 play log lines, reaction jokes and color names now live in `mods/uni/texts/<locale>.json` as `uni:log.<name>` (190 keys in each of the nine locales). The client reads them through the mod text layer, and the 520 old `log_*` keys are removed from the ten message catalogs (pseudo included). A golden of every key in all nine locales, made before the move, passes unchanged after it. A second golden pins the variant choice for 27 scenarios and the sequence numbers 0 to 63, so the FNV-1a pick does not change. No log text fell back to English before the move, so no text needed a rewrite; a test now proves zero fallback, with a small allowlist for digits and `Color` in Spanish. The tag balance check now covers all `uni` copy (log, reaction, glossary and card texts) in all locales. Interface texts stay in Paraglide. Ticket 23 of the event contract.
- **Core glossary and card texts moved into the `uni` mod** — the 15 core glossary entries and the 54 base card labels and descriptions now live in `mods/uni/texts/<locale>.json` (138 keys in each of the nine locales: de, en, es, it, ja, ko, ru, uk, zh) and `mods/uni/glossary/` (15 entries). The client reads them through the mod glossary and the mod text layer, and `GLOSSARY_REGISTRY` is gone. The 43 old message keys in each of the ten message catalogs (pseudo included) are removed; the interface texts and the `glossary_tag_*` pill labels stay in Paraglide. A golden snapshot of every glossary entry and card text in all nine locales, made before the move, passes unchanged after it. The `metadata` response now also carries a `mods` listing (texts and glossary of the loaded mods), so the lobby shows the glossary before a match starts; a match `defs` packet replaces it. Card descriptions return nothing until the texts load, so a raw key never shows. Tests check that every locale has the same keys and placeholders as `en`, that the plural categories fit the locale, and that no moved key stays in the message catalogs. Ticket 22 of the event contract.
- **Glossary entries for mods** — a mod can ship glossary entries in `mods/<id>/glossary/<name>.json` (`uni:glossary_entry` with a `uni:label_key` and a `uni:description_key`). An entry holds keys only, never text. Every card definition also makes its own entry (keys `<card>.label` and `<card>.description`, or the optional `label_key` and `description_key` of the card), so `[k=my_mod:foo]` works for a mod card with no glossary file. An explicit entry replaces the automatic entry of the same id inside the same mod. An entry in a foreign namespace, a `uni:` id from another mod, a duplicate id, or text in a key field rejects the mod at load. The glossary keys count as used keys: a key missing in `en.json` rejects the mod, and a key missing in another locale only warns. The `defs` packet lists the entries (id, keys, source). The client looks up the core catalog first and then the mod entries, resolves the text through the mod text layer, re-renders on a locale change, and ignores an entry whose id namespace differs from the mod that listed it. Core `uni:` glossary texts stay in Paraglide until the next step. Ticket 21 of the event contract.
- **Mod text layer** — a mod can ship texts in `mods/<id>/texts/<locale>.json` (a flat map from key to string, in the mod's own namespace). Texts are a new slot kind in the asset pipeline: the `defs` packet lists each file with its locale, SHA-256 hash and size. The server validates them as the last stage: a malformed file, a non-string value, a key in a foreign namespace, a symlink, or a file over 256 KiB (value 2 KiB, 5000 keys) rejects the mod. A key that a binding uses and that is missing in `en.json` rejects the mod at load. A missing key in another locale only warns. The client fetches only the active locale and `en` for each mod, checks the SHA-256 before it parses a file, and falls back on a mismatch. It validates the mod id and the locale before it builds a fetch URL. A locale change fetches the new files and renders again. The formatter supports `{placeholder}` values, the rich text tags, and plurals through `Intl.PluralRules`, with no gender and no `select`. The fallback is the active locale, then `en` with a warning, then the raw key with an error, and each message is logged once. The `pseudo` locale is derived from `en` and is never fetched. Mod texts are loaded after the interface texts, which stay in Paraglide. The dev server now allows only the mods and test fixtures folders next to the frontend. Ticket 20 of the event contract.
- **Presentation bindings and a separate play log queue** — a new file kind in the format version 1 style (`presentations/*.json` with the top-level key `uni:binding`) binds a presentation to an event id. Each binding has up to eight cases. A case has tests (`equals`, `in`, `exists`, `has_tag`, and `relation`, which is allowed only in presentations: the viewer is the actor or the target, `self` or `other`) and outputs: animation beats, sounds, a play log line (text variants and parameters, picked with the existing FNV-1a hash over the sequence) and a glossary reference. A mod binds to its own events and to core events. The validator rejects an unknown event, a field with the audience `none`, an unknown beat or sound, and warns on a field that only some viewers receive. A mod with no presentation loads and plays. The `defs` packet lists the bindings. The client runs them on live client events only, so a replayed tail never animates, and sounds stay silent while the animation queue fast-forwards. The play log now has its own queue (`logQueue.ts`), so a skip or a flush of the animation queue never drops a log line. The test mod shows its own beat, sound and log line for `test:apple_eaten`, with a different line for the actor. Mod sounds validate against the asset bundle but stay silent until the sound player reads mod assets. Ticket 16 of the event contract.
- **Remaining streaks, derived events and the end of the client counters** — the other counters now live in the engine: `uni:draw_run` and `uni:total_draws` (from `cards_drawn`), `uni:wild_run`, and `uni:last_wild_color` (the last and the previous wild color). A new file kind, `derived/*.json` (only the `uni` mod may define it), holds derived events. `near_win_entered` and `blocked` are core client events with a `cause` (the sequence of the base event), audience `all` and retention `log`. The near-win window opens when a player plays down to one card with a card that has no `uni:draw_penalty` tag, and closes when that seat plays, draws, is skipped or wins. `blocked` fires on a skip of that seat or a forced draw, and it reads the window from before the event. A derived event never feeds a streak, and the derived pass counts against `UNI_STREAK_COST_MAX`. `context.near_win` in the warm-up snapshot gives a late joiner the same window as a continuous player. The client now reads every counter from the envelope `context` and the derived events, and the old `StreakTracker`, its tests and `StreakInfo` are deleted, with a guard test that fails if they return. Parity tests compare the server with the stored values of the old tracker over ten seeded bot matches and five hand-driven scenarios. A penalty draw from a stack now counts once, from `cards_drawn`, and not from the debt status line. Ticket 15 of the event contract.
- **Streak mechanism, with skip and reverse streaks** — a streak is a counter that the engine updates from client events. A mod can define streaks in the format version 1 style (`streaks/*.json`) with the types `run`, `total` and `last_value`, an optional `group_by`, and `reset_on` matchers. The matcher is small and closed: `equals`, `in`, `exists` and `has_tag` (`relation` is for presentations only and a streak that uses it is rejected). Card kinds now carry tags (`uni:skip`, `uni:reverse`, `uni:wild`), and `card_played` gives the tags of the played card. A streak reads only payload fields with the audience `all`, and only events that reach every seat can feed a streak, so the public values cannot leak a hidden fact or the occurrence of a one-seat event. The engine runs one evaluation pass for each base event, skips derived events, and limits the cost (`UNI_STREAK_COST_MAX`, default 2048 each turn) and the size of the context (`UNI_CONTEXT_SIZE_MAX`, default 512 bytes). A streak that the cost limit skipped is left out of the context. The server attaches `context` to the envelope of events that ask for streaks (`turn_skipped` gets `uni:skip_run`, `direction_changed` gets `uni:reverse_run`), and `context.streaks` in the warm-up snapshot holds the current values of every loaded streak. The `streaks` stage of the validator now runs after the `events` stage. The play log reads the skip and reverse streaks from the context, with the old local computation as a fallback. A parity test over three seeded bot matches shows the server values equal the old client values. Ticket 14 of the event contract.
- **Mod event budgets and `emit_signal` migration** — four environment variables limit mod events: `UNI_MOD_EVENTS_PER_TURN` (default 64), `UNI_MOD_EVENT_PAYLOAD_MAX` (1024 bytes), `UNI_MOD_CAUSE_CHAIN_MAX` (16) and `UNI_MOD_VIOLATION_TURNS` (3). A bad value (junk, negative) falls back to the default, and a huge value is clamped. The server drops excess mod events and logs them at error level, one line for each mod, budget and turn. Core events and their alias copies are never dropped, and per-seat sequences keep no gap. A mod that breaks a budget in the violation count of distinct turns is disarmed through the existing path, and `mod_disarmed` is emitted once with no audience. The cause chain length is 1 plus the length of the mod event that the `cause` names, so one play keeps at most 16 chained mod events. The older `UNI_CHAIN_BUDGET` and `UNI_EVENT_BUDGET` behave as before. `emit_signal` keeps working for rules and takes optional `event` and `fields` arguments that also emit a defined mod event for presentations, through the same checks as `emit_event`. A golden file pins three flood scenarios (per-turn flood, chain flood, repeat offender). Ticket 13 of the event contract.
- **Mod event definitions and the namespace rule** — a mod can define its own client events in the format version 1 style (`events/*.json`: `uni:format_version`, `uni:event`, payload fields with a type and an audience, a retention). A new op `emit_event` emits one. The server drops the event, with a log line, when the id is unknown, when the mod does not own the namespace, or when a value has the wrong type or is too large (string 256 bytes, payload 1024 bytes, 16 fields). Fields with no declared audience are never sent (default `none`), and `target` and `owner` roles come only from fields of type `seat`. The wire packet is `mod_event` with the namespaced `id`. The client ignores it unless a presentation binds to it. The event id, the retention and the field names in the `defs` manifest are public: do not put a hidden fact in them. A mod defines ids only in its own namespace, and only the `uni` folder can use the id `uni`: a mod that defines a `uni:` id, a foreign namespace or a core event name is rejected at load. The `defs` packet now lists each mod with its version and a SHA-256 content hash, and the client cache key is `id@version#hash`. The semantic validator now runs at load in the order uni, events, streaks, presentations, texts, and rejects a failing mod. It needs `contract/schemas` at run time (`UNI_SCHEMA_DIR`). The loader reads old-style and new-style files together. Golden files pin the mod event packets for each viewer and the `defs` mod list. Ticket 12 of the event contract.
- **Retention and the event tail** — every core event now has a `retention` (`transient`, `until_turn_end`, `until_round_end` or `log`) in one table next to the audience table. The server stores each logical event once (`EventTail`) and projects the retained events for a late joiner with the same projector as live events, so a late seat or spectator never sees a card identity it could not have seen live. The tail travels in `match_state.tail` as a self-contained list with its own numbering and `replayed: true` on each envelope, so it never disturbs the live sequence. A turn ends at the engine turn counter, so a jump-in or an extra turn drops its events at the next turn advance. `UNI_EVENT_TAIL_MAX` (default 200, maximum 10000, 0 means no tail) caps each class. The client feeds the tail to the play log only, once for each match, with no animation, no sound and no timer restart. A golden test (`tests/fixtures/golden_tail/late_join_tail.json`) pins the tail of a late seat, and a second test compares it with a continuous seat event by event. Ticket 11 of the event contract.
- **Warm-up snapshot context** — `match_state` now holds a `context` object with the engine-owned values, so a late joiner needs no reconstruction: `active_color`, `direction`, `draw_debt`, `hold` (the play-drawn hold: other seats see `held` only, the owner also gets `card`, spectators get none), `timers` (turn and window deadline and remaining time, never negative) and `last_event_sequence` (the next live event continues from it). A hidden draw debt counts only for its owner, like `status_applied`. The `streaks` map is empty until the streak tickets fill it. The client reads the context first and keeps the old top-level fields as a fallback. Tests drive a real recipient sink through a played match and compare it with a late-join snapshot. Ticket 10 of the event contract.
- **Window, prompt, roll, status, placement and match end client events** — the remaining core events now exist under the new names, beside the old names until the old ones go away. `window_opened`, `window_responded` and `window_closed` (`targets`, `target`, `window_id`) replace the window events, and a losing response still hides its card from other seats. `prompt_opened` and `prompt_answered` are sent to the target seat on the same per-seat sequence counter, right after the old prompt packets. `outcome_rolled` carries a public roll ordinal and never the engine counter. `status_modified` is new (`modify_status`), and a hidden status reaches its target only. `placement_set` (`target`, `place`, `is_elimination`) and `match_ended` (`target`, `targets`, `final_digest`) are public. `is_elimination` is false for now, because the engine has no elimination. `play_rejected` gains `target`. The diagnostic events `chain_aborted` and `mod_disarmed` now go to no seat and no spectator and take no sequence number. They stay in the server log. The match engine queues the old event and then a copy under the new name, so these packets double on the wire until the old names are removed. The client reads `placement_set` and `match_ended` for the win line, with the snapshot path as a fallback and no double line in either order. Ticket 09 of the event contract.
- **Hand and card client events** — seven new core events: `hands_swapped` (`source`, `target`, `count`, `target_count`), `hands_passed` (`direction`, `targets`, `counts`), `hands_redistributed` (`targets`, `counts`), `card_moved`, `card_created`, `card_removed` and `card_replaced`. The ops (`swap_hands`, `pass_hands`, `redistribute_hands`, `move_card`, `transfer_card`, `materialize_card`, `remove_card`, `replace_card`) emit them, so any mutation that still reaches a primitive emits them too. The swap and pass events replace the old server-authored `hands_swapped` and `hands_passed` payloads (fields `a`, `b`, `a_size`, `b_size`, `players`, `hand_sizes` are gone, payload version 2) and the old NOTE markers. Hand counts are public. Redistribution sends counts only. The identity of a moved card is decided when the event is made, from its destination: only the owner of the destination hand sees it, a move into the discard pile is public, and a move into the draw pile or limbo reaches nobody. This closes a case where a seat could see a card inside an earlier move event. Created and replaced cards never send a kind. The client keeps the same hands animation from every seat, and the other five events map to a no-op beat. A card play stays one `card_played` event, with no extra `card_moved`. Ticket 08 of the event contract.
- **Draw and keep client events** — `cards_drawn` now carries `target`, `count`, `hand_size_after` and `from_debt` (true when the draw pays a draw debt), with the causer as the envelope `actor`. The old fields stay beside them until the old names go away. The card list is visible to the drawing seat only (and to a spectator when the owner has privacy mode off). A new `drawn_card_kept` event ends the play-drawn choice: `target` is public and `card` goes to the owner only. A turn clock that decides for the seat also emits it, with the cause `turn_timer_expired`. The client no longer infers a keep from snapshots when the event is present, and the draw line reads `hand_size_after` from the event. A `cards_drawn` with a count of 0 no longer logs a line or plays a sound. The penalty line still fires on the `draw_debt` status, so it does not wait for a card to move. A mutation that replaces or wraps `draw_cards` still emits one `cards_drawn`. Ticket 07 of the event contract.
- **Turn-flow client events** — six new events follow the `direction_changed` pattern: `turn_skipped` (target is the skipped seat), `extra_turn_granted`, `active_type_changed` (`color`, `previous_color`), `restriction_added`, `restriction_removed` and `turn_timer_changed` (`duration_ms`). The primitives emit them, so any mutation that still reaches a primitive emits them too. The skip flag now remembers its actor (`TurnState::skip_actor`), and `turn_skipped` marks the turn advance that skips, before `turn_advance`. `set_active_type` emits nothing when the color does not change. Restriction events carry only `restriction_id` and `phase`, never the condition. `turn_timer_changed` marks a change made by the `set_turn_timer` op only. The engine's own arm and reset of the turn timer emit nothing. The client reads the wild color log line and the ripple from `active_type_changed`, and the skip line and X mark from `turn_skipped`, with a dedupe in both orders. The old guesses from snapshot changes stay as a fallback. Ticket 06 of the event contract.
- **`direction_changed` event, end to end** — the `reverse_direction` op now emits a `direction_changed` event with the new `direction`. The actor comes from the causal frame. Every seat and spectator gets it (audience `all`). The client maps it to the direction ring and to the reverse line in the play log, with no guess from turn changes. When the event is present it wins, and a reverse never shows twice, in either order of the event and the turn beat. Without the event, the old guess from a turn change still works until the old names go away. Any mutation that still reaches the primitive still emits the event. This is the tracer for the core events: the steps are in a comment block in `event_audience.hpp`. Ticket 05 of the event contract.
- **Spectator privacy projection** — each user now has a Streamer/Privacy Mode setting (`users.privacy_mode`, migration v11, default off). A spectator sees the hidden cards of a seat only when the owner of that seat has the setting off. When it is on, the spectator sees card backs for that seat. Each seat is decided on its own. The draw pile order never reaches a spectator, and the omniscient spectator snapshot is gone. A setting change applies to spectators who join afterward and from the next client event for current spectators. A guest keeps the setting on the socket only. `/auth/me` and `/auth/login` return `privacy_mode`, and the client syncs it. `MatchSession::DemoteSeatToSpectator` is the seam for an eliminated seat. Nothing calls it yet, because the engine has no elimination. The spectator leak tests scan every spectator frame for draw pile cards and for the cards of private seats. Ticket 04 of the event contract.
- **Audience framework and per-seat redaction** — every payload field of the core events now has a declared audience (`all`, `actor`, `target`, `owner`, `none`) in one table (`event_audience.hpp`). Any field or event that is not in the table sends nothing. A new per-seat projector (`event_projector`) applies the table in the event sink and the view builder. It removes the instance bits and the entity `{index, generation}` of a card that the seat must not see. A losing `window_response` shows its card only to its owner. The public roll counter is now a per-stream ordinal, so it no longer gives the number of hidden random draws. In production (`UNI_PROD_BUILD` or `UNI_ENV=production`) the server ignores `UNI_SEED` and refuses to boot when it is set. A leak scanner tool (`leak_scanner.hpp`) searches every frame and snapshot of full bot matches for hidden card bits and handles. Free-form `signal` and prompt payloads only get the handle scrub. Ticket 03 of the event contract.
- **Client event envelope, per-seat sequence and snapshot request** — each client event now carries `sequence`, `id`, `actor`, `cause`, `turn` and `round` beside the old `seq` and `type`. Each seat has its own `sequence` counter, and session packets (defs, prompts, barrier) take numbers from the same counter, so an event for one seat leaves no gap for the others. `cause` is the sequence of the causing client event as this seat knows it, and it is left out when the seat never received that event. The engine gives every event an internal `event_id` that never reaches the wire. A new client message `match_snapshot_request` asks for a fresh snapshot. The server answers with `match_state_updated` and a `seq_watermark`, and limits requests for each recipient (`SNAPSHOT_REQUEST_MIN_INTERVAL_MS`, default 1000 ms). The client asks once on a real gap and retries after silence. Only `card_played` starts a cause chain for now. Ticket 02 of the event contract.
- **Turn counter and causal frame** — the engine now counts turns (`MatchMeta.turn`, grows at each turn advance) and stamps every engine event with a causal frame: `actor`, `cause`, `turn` and `round`. An op in a rule chain passes the frame of its entry point to the events it emits. System events have a defined actor: a turn timer expiry names the seat whose clock lapsed (`turn_timer_expired`), a window timeout has no actor (`window_timeout`), a bot move uses the seat of the bot with the same cause as a human move, and engine defaults (round advance, scheduled chains) have no actor (`system`). No wire change: the session still sends only `type` and `payload`. First step of the event contract (ticket 01).
- **`uni` mod id accepted beside `vanilla`** — expand step of the `vanilla` to `uni` rename. A new helper (`modload/base_mod.hpp`, `utils/baseMod.ts`) treats both ids as the base mod. The stats gate, the lobby deck and rule settings, the match assembler and the frontend lookups (card kinds, glossary, draw debt status, play log, classic deck) accept both. A deck that lists both ids is rejected as a duplicate. Nothing is renamed yet (tickets 18 and 19).
- **Mod `vanilla` renamed to `uni`** — migrate step. The mod folder is now `mods/uni`, and every kind, deck, status and rule id uses the `uni:` namespace (`uni:zero`, `uni:draw_debt`). The glossary keys, the `[k=uni:...]` keywords in all message files and the frontend lookups follow. Hard break: a stale client deck with `vanilla:` kinds fails with `assembly.unknown_kind` and nothing crashes. No saved data holds a mod id, so no migration code exists. The legacy acceptance helpers stay until the contract step (ticket 19).
- **Old mod name `vanilla` removed** — contract step of the rename. The legacy acceptance helpers (`IsSameModId`, `ToLegacyBaseKindId`, the legacy kind map in the stats gate, the uni-to-vanilla fallback in the match assembler, `DRAW_DEBT_STATUS_KINDS`) are gone. A deck or lobby that lists the old mod id fails with `assembly.unknown_mod`. `VanillaClassicDeckCards` is now `UniClassicDeckCards`, the manifest name is "UNI", and prose that used the old word now says "default". Two guard tests (`uni_rename_test.cpp`, `uniNamespace.test.ts`) fail if the old name returns to source, mods, tests or messages.
- **Client `outcome.resolver`, face-override rendering and dice acceptance** — the entity runtime's final SP2 consumer lands. A new `entity/outcomeResolver.ts` selects an entity's `outcome.resolver` descriptor for the bound `$last_outcome` key by exact match, then `*`, then no-op (never throwing), and the `spawnEntity` IR op reads that key from the step (`payload.outcome` or `payload.last_outcome.key`) and stores the resolved descriptor on the entity's state as `state.outcome` (with `state.outcomeKey` always recorded) for the owning subsystem. The read-only `OverrideChannel` now also resolves a modifier's `source` (`seeded-random-tile`) through an injected per-client cosmetic RNG, and `CardMesh3D`/`AtlasLayerMesh` consult `storeAffliction.override(cardId)` — a UV/texture-only face swap via `faceKeyForOverride`/`entryWithOverride` in `cardFaceAtlas.ts`, no shader or material parameters (SP3). A new `stores/affliction.svelte.ts` owns the defs→presentations bootstrap (ruling: consume `defs.entities[]`/`presentations[]`, define them in the shared `EntityRuntime`, register each presentation through `PresentationTriggers`, honour the reduced-motion/animation gate, dispose on teardown) and makes the otherwise pure `AfflictionManager` reactive through a `$state` revision. `mods/test/rules/dice_roll.json` ships the server-side `roll_table` chain, and the dice lifecycle (`spawn → systems tick → ttl → despawn` plus key→descriptor selection) is accepted both backend (`entities_presentations_test.cpp`) and frontend (`diceAcceptance.test.ts`).
- **Presentation triggers and raw match-event fan-out** — mod presentations now install themselves client-side when their declared source fires (SP2 §12.2). `PresentationTriggers` routes a presentation's `triggers[]` by source — no `on` installs at registration (always-on), `status:<id>` installs when the server reports that status, and any other `on` is a `match_event` type evaluated against the raw payload — activating or expiring the affliction through the existing `AfflictionManager`. Conditions are the pinned SP2 minimum: `where` is a flat map of scalar **equality** against the payload (no operators), an empty/absent `where` is unconditional, and unknown event types are ignored (forward-compatible, never thrown). A `duration` schedules expiry in seconds; no duration is persistent until an off event/despawn. Installs honour the reduced-motion / animation toggle through an `enabled` gate. `storeGame.onMatchEvent` now fans every raw `match_event` `(type, payload)` frame out to subscribers (alongside the normalized-beat fan-out), and `wirePresentationTriggers` bridges that stream to a trigger set. Each subscriber is called under its own `try/catch`, so one throwing handler cannot abort the frame for the others or the beat buffer.
- **Affliction composition engine, manager, override channel and cosmetic RNG** — the client can hold several active presentations per target and fold them into one resolved map. A pure `compose()` applies the two §12.1 stages against the engine kind→merge-mode table (content never carries a `mode`): a target that carries any `exclusive` kind becomes single-owner (the max `(priority, activationSeq, id)` holds, the rest drop), then each kind merges per `(kind, target)` — `face.override`/`material.param` take the highest-ranked modifier while losers stay active (reversible), `screen.effect`/`particles.emitter` stack in priority order under a cap that drops the lowest, `audio.loop` is exclusive. `AfflictionManager` rejects exclusive losers at activation (they are never stored, unlike override losers), recomputes on every activation/expiry, owns the monotonic activation sequence, and resolves by `kindTarget`. A tolerant `OverrideChannel.override(cardId)` reads the winning `face.override` for `CardMesh3D`/`AtlasLayerMesh` to swap a face's `texture`/`uvRect`. `CosmeticRng` adds a deterministic per-client splitmix64 stream (string-seeded, independent of `Math.random` and every other instance) for `seeded-random-tile` face sources, and never enters `storeGame.state`. `audio.loop` composes (exclusive, single-owner per target) but no consumer drives it — `storeAudio` has no loop API — so playback is deferred to SP3.
- **Per-step watchdog, queue cap and FX budget store** — the IR queue can no longer be wedged by an effect that never finishes. Each blocking beat arms a `setTimeout` watchdog (default 8000 ms, `UNI_FX_STEP_TIMEOUT_MS`); when it fires, every step is skipped and the queue advances — which also clears the documented `timeScale(0)`/NaN frozen-timeline wedge. `enqueue` now caps the pending backlog (`UNI_FX_QUEUE_MAX`, default 64): at the cap the oldest _waiting_ batch is dropped with a warning and its promise resolved (never the batch currently playing). A new `stores/fxBudget.svelte.ts` exposes the runtime budget (`UNI_FX_MAX_ENTITIES` 64, `UNI_FX_MAX_AFFLICTIONS` 8, `UNI_FX_SCREEN_STACK_MAX` 3, `UNI_FX_STEP_TIMEOUT_MS` 8000, `UNI_FX_QUEUE_MAX` 64), read from `import.meta.env` (Vite `VITE_`-prefixed or bare) with those fallbacks and writable at runtime. The caps are wired into their consumers: `GameBoard` builds the shared `EntityRuntime` from `maxEntities` and the affliction store builds its manager from `maxAfflictions`/`screenStackMax`, while the spec's bare `UNI_FX_*` names reach the client build via Vite's `envPrefix`. A malformed, fractional, zero or negative override is floored to an integer and clamped to a positive minimum, so it can never silently disable a cap.
- **Client IR effect handles, step grouping and entity ops** — the animation step schema (`AnimationStep` in `animation/types.ts` and `contract/asyncapi.yaml`, kept in lockstep) gains an optional `groupId`, so a composite effect's sub-steps share one start position. Renderers may now return an `EffectHandle` (`{ skip(), blocking, persistent }`) instead of only a GSAP timeline: the queue advances on `blocking` alone, so a persistent effect (a spawned entity, a one-shot sound) detaches to its subsystem and never stalls the beat; `skipCurrent()` now calls every step's `skip()` before advancing, and the reduced-motion / animation-off path skips every step. A public `CardRegistry.register(op, renderer)` seam plus six new ops — `spawnEntity`, `despawnEntity`, `setEntityState`, `attachTo`, `emitParticles` (SP3/SP4 stub), `emitSound` (via the audio store) — wire the cosmetic entity runtime into IR playback; the runtime is injectable (`createCardRegistry(runtime)`) and exposed as `registry.entityRuntime`, so the future entity renderer shares the exact instance the `spawnEntity` op spawns into; an unknown op or an unresolvable entity target still degrades with a warning and never throws into the queue.
- **Entity & presentation ingest (`entities/`, `presentations/`)** — the second artifact of the mod SFX programme opens two new fail-closed content entry sets. Mods declare namespaced entity types (`contract/schemas/entities.schema.json`, `entities/<id>.json`) composed from the §4.2 component vocabulary (visual / movement / motion / emitter / audio / outcome / camera / screen / lifetime), and targetable presentations or afflictions (`contract/schemas/presentations.schema.json`, `presentations/<id>.json`) whose per-kind merge behaviour is engine-owned (content carries no `mode`) and whose triggers are evaluated client-side. Both mirror the SP0 `tables/` ingest exactly: sorted per-file scan, a missing directory is an empty set, and duplicate local ids, malformed entries or missing `components`/`target`/`modifiers` block the mod with a readable error. `mods/test` ships a `dice` entity and an `lsd` presentation as the proving content; semantic family, kind, target and outcome-resolver checks follow.
- **Entity & presentation semantic validation** — the mod SFX validator now enforces the entity/presentation contracts fail-closed, not just their ingest. An engine-owned component→family table rejects an entity declaring two components of one family (`entity.component.family`), and unknown component names or an empty component set are errors; presentations validate each modifier kind against the engine kind→merge-mode table, reject a stray `mode` field (merge mode is engine-owned), check the target grammar, reject an empty modifier list, and validate trigger `on` sources (`always` / a known `match_event` type / `status:<id>`) with every `where` key checked against that source's payload; a D15 cross-check requires every key an outcome table can emit to have an entity `outcome.resolver` entry (or a `*` catch-all anywhere in the set); and entity component references into asset slots (`sound`/`shader`/`sprite`/`model`) must resolve in a bundle the mod ships. `mods/test` gains a small `sfx` asset bundle so the dice sample's `roll` clip resolves.
- **`defs` entity & presentation manifests, and mod API version 2** — the `defs` packet now publishes the active mods' entity and presentation definitions as `entities[]` and `presentations[]` (SP2 §7.4). Each entry is hash-addressed — `{ id: "namespace:local", def_hash, ...full def }`, the def's canonical JSON plus a 16-hex content digest — so the client caches each def by content and reloads only what changed; `defs_digest` folds both arrays in so a stale cache is still detectable. Unlike SP0 tables (which have no client consumer), these are published because the client entity runtime resolves them by def id. `MatchRegistries` gains per-match `entities`/`presentations` registries frozen by `wiring::LoadEntities`/`LoadPresentations`, mirroring `LoadTables`. `contract/asyncapi.yaml` gains the `EntityDef`/`PresentationDef` shapes and bumps `ModApiVersion` 1 → 2, because a mod using `entities/` must not load on an engine that would silently ignore the directory (`mod.unknown_entry` is only a warning).
- **Client entity-component runtime** — the frontend gains a lightweight, pure-TypeScript entity-component runtime (`components/game/entity/`) that replays mod-declared entities cosmetically and never touches game state. A `ComponentRegistry` classifies the §4.2 component vocabulary into families; `EntityRuntime` spawns defs by id (`spawn("dice", { as: "d1" })`), resolves `entity:<handle>` addresses, tracks a live count against a cap (default 64, constructor-injectable so a runtime FX budget can wire in without rework), and ticks pure movement (`arc`/`linear`/`ballistic`/`static`), motion (`spin`/`oscillate`/`jitter`/`spring`/`follow`) and lifetime (`ttl`/`untilDespawn`) systems before despawning expired entities. Movement anchors (`$origin`/`$target`) resolve through the same handle/anchor map as `attachTo`/`resolve`; an unknown def, unknown component, missing anchor, over-cap spawn or duplicate handle all degrade with a warning and never throw. An `EntityRenderer` seam (SP2 ships a no-op) lets SP3/SP4 drop in real rendering. The shared runtime is advanced every frame by `AllCards3D`'s `useTask` (the same delta that ticks the card registry), so spawned entities move and a `lifetime.ttl` entity despawns instead of silently consuming the cap.
- **Outcome tables (`tables/`)** — the first artifact of the mod SFX programme. Mods declare named weighted tables as content (`contract/schemas/tables.schema.json`, one `tables/<id>.json` per file, sorted, fail-closed); the server ingests, validates and rolls them. A new `roll_table { table, into? }` op performs a weighted pick on the match's existing seeded splitmix64 stream (never a new RNG), supports an optional nested 10.6 `roll` and an optional 10.2 `where` filter, binds `$last_outcome {table,key,value}` and emits a new `outcome_result` packet (`{table, key, value, counter}`, `key` pre-namespaced `mod_id:key`). New per-outcome accessors `outcome_is` / `outcome_value` let rules branch on a rolled key/value rather than only the rolled total; raw `roll_result` is unchanged. Validation stays fail-closed: unknown tables, duplicate keys, `weight < 1`, `value`+`roll` together, out-of-range nested rolls and dangling `where` keywords all block the mod with a readable error.
- **Draw-stacking stack answered and stack taken lines** — the `draw_stacking` mod now owns the answered line and the `uni` mod owns the taken line. A winning `window_responded` whose window kind is `generic` posts `draw_stacking:answered_1` or `draw_stacking:answered_2` with the responding seat (identifier `draw_stacking:answered_log` in `mods/draw_stacking/presentations/stack_answered.json`). A `cards_drawn` with `from_debt: true` now posts `uni:log.stack_taken_1` or `uni:log.stack_taken_2` instead of the large-draw line: `mods/uni/presentations/draw.json` gains a first case on `from_debt`, placed before the large-draw case so `uni:select: first` picks it. The answered line reads the window kind that Task 1 fixed to the stacking member, which is the generic default for `draw_stacking`, so it does not also fire for a `jump_in` window. The English texts ship; the other locales follow with the standard-mod translation sweep.
- Creative P2: Director x-ray for the host, director panel with clean view and hide players, lobby creative toggle with scene picker.
- Creative P3: Director pause, step, and force turn with all turn timers cancelled together.
- Creative P4: Scripted bot intents with heuristic fallback, draw pile stacking with registered wire identities.
- Creative P5: Dev-only scenes applied at match start, in-session scene loading, checkpoint save and restore of visible state.
- Creative fixes: host x-ray snapshot carries all hands, malformed director args rejected with specific errors, full clean view hides panel and chat, director console with transport/script/stack/seat/checkpoint/scene controls, seat rename/skin/flip, lobby Creative mode picker entry.

### Changed

- **Wild play line shows the chosen color** (test) — the play parity fixtures now post the play line of a wild on `active_type_changed`, caused by a tagged `card_played`, and read player and card from the cause event (`uni:cause`). The parity test proves the same line and color as the core.
- **`card_played` payload declares `tags`** — the contract now lists the optional field `tags` (array of strings, the tags of the card kind) in `CardPlayedPayload`. The server already sent it. The packet version stays 1 because the field is additive.
- **Play log parity gaps closed** (test) — the new binding features (card part, actor test, `finished` test, `caused_by` test, log param default, cause source) now close the parity gaps G1 to G4, G6 and G8 (the timing of the play line of a wild, closed by the cause source). Two differences stay as accepted: G5 (the near-win order) and G7 (an auto-played wild keeps the registry color).
- **`placement_set` carries `finished`** — the field is additive and the packet marker stays 1. `finished` is true when the place ends the race for that seat.
- **`window_responded` carries `window_kind` and `outcome`** — the fields are additive and the packet marker stays 1. `window_kind` is the kind of the window member that owns the response (`window->kind` for a pass). `outcome` is `winning`, `lost` or `pass`. A mod binding can read both fields. A lost response shows the group leader kind in `window_kind`, not the kind of its hidden card.
- **Translated the standard mod log lines** (texts) — the four standard mods (`draw_stacking`, `jump_in`, `seven_zero`, `progressive`) now ship all nine locales, and `uni` gained the two `uni:log.stack_taken_*` keys in the eight non-English locales. The no-fallback test covers the four mods and each mod's key parity across locales. The ru/uk number forms match the shipped precedent; the keys flagged for native review are recorded in the follow-ups sweep report.

### Fixed

- **A stack answer reports the stacking member kind** — a winning `window_responded` whose card stacks now carries the stacking member's kind in `window_kind` (the member the route half reroutes to), instead of the owning member's kind. Before this, a +2 answered by a +2 in a merged `jump_in` + `draw_stacking` group reported `jump_in` with a winning outcome, so both the jump-in line and the stack line posted.
- **The reconnect tail runs the bindings** — a late joiner's replayed history now runs the presentation bindings with the same cause memory as the live path, so mod lines (jump_in, seven_zero, progressive, draw-stacking stack) appear in the log after a reconnect instead of being dropped. Only log lines post: no beats, no sounds, no timers. The tail also honours the repeat guards (direction, placement, winner) plus a per-replay dedupe, so a repeated envelope posts nothing twice. The core rules keep running first, the same order the live path uses, and a card that carries the `uni:own_log` tag keeps the core rules out of its own lines.
- **Window and schedule resume keep the play cause** — a graph resumed by a response-window route or an armed schedule now runs inside the play chain that parked it, so its events and any prompt it parks name the original `card_played` as their cause. Before this, the close scope reset the cause and the resume overwrote the frame, so a prompt opened after a response window or a schedule leg lost the cause. The window pause carries it like the input pause does, the schedule envelope stamps it when the arm runs inside a play, and an entry point outside a play still stamps no cause.
- **An auto-played card leads with its own play** — `AutoPlayCard` now opens its own causal scope and emits `card_played` and `auto_played` BEFORE its behaviour-graph events, so the graph events of an auto-triggered card name that card's own `card_played` as their cause, and a prompt parked by them keeps it. Before this, the graph events carried the enclosing play's cause and an auto-played wild posted one line with the registry colour `white` (parity gap G7). The `auto_played` payload also gained the `tags` field, so the `auto_play` binding can skip a wild and post one line with the chosen colour.
- **Mixed +2/+4 draw stacks** — a +4 can answer a +2 window and a +2 can answer a +4 window, in any colour and on any combination of the two. The `plays_stack_response` gate now checks only that the response card and the top of the discard both carry the stackable tag, instead of also requiring the same draw value. Before this, the eligibility of a mixed stack was false, so the snapshot marked the card `can_play: false`: the client dimmed it, blocked the drag and the keyboard, and the victim could not stack a +4 on a +2 even though the engine accepted the response.
- **Color change keeps the play cause** — a wild color change after the color prompt now carries `cause`, the sequence of the wild play. Before this, the cause was lost over the prompt. Bindings need it for the `caused_by` test.
- **Held stack close keeps the responder** — the engine now closes a held group with the recorded winner as actor, so the stack line `status_applied` from a merged `jump_in` and `draw_stacking` group names the player who responded.
- **Play log test harnesses use the production binding ids** (test) — the shipped-registry harnesses in `uniLogBindings.test.ts` and `playLogParity.test.ts` injected `uni:<stem>` as each binding's id, but the server keys a binding by its `uni:binding.uni:description.uni:identifier` (`ParseBindingEntry` sets `binding_id` from it; the registry and the `defs` packet are keyed the same way), so the ids the tests used were never sent. `stableIndex` salts the variant pick on that id, so no multi-variant pick a test observed matched what a live client renders. Both harnesses now inject each file's own identifier and assert it exists, and the Task 3 near-win assertion is pinned to its exact variant again instead of a prefix match.
- **The degraded unresolvable-card play line is named as such** (test) — a `card_played` whose card bits the viewer cannot resolve still posts `uni:log.play` with raw `{kind}`, `{color}` and `{cardName}` placeholders, because a binding has no way to require a resolvable card-part param. The core path dropped the line instead. The case that pins this is renamed to mark it an accepted degradation, the trigger is documented as narrow (the live path resolves bits for every viewer through the shared `defs` registry, so only a defs-lagging late joiner reaches it), and follow-up item 36 records the missing feature: a `resolves` test word or a required-param drop.
- **A binding names a streak that exists, and a card part it can read** — a binding test or log param that reads `uni:streak` was shape-checked only, so a mistyped id (`uni:draw_runn`) loaded clean and read as missing at run time; the validator now rejects it with `binding.streak.unknown` against the streak ids the loaded mods declare, deferring a reference to a mod that is not in the set as the streak-context check already does. A log param whose `uni:card_part` is neither `kind` nor `color` fell through to a plain field read and rendered the raw card integer; the client now yields nothing for such a param, so its `uni:default` applies. Validator and client only, no wire change.

### Removed

- **The orphaned selection golden fixture** (test) — `tests/fixtures/golden_log_texts/selection.json` held a frozen snapshot of the core log line selection. The play log became bindings only, so nothing reads it any more: the text golden reads its files by locale name and the no-fallback test reads per-locale names. The nine locale goldens and `rewritten.json` stay.
- **The core play log path** — the play log is bindings only. The core rule engine (`logBuilder.ts`, `reactionRules.ts`, `reactionContext.ts`), the emitter (`playLogEmitter.ts`), its queue (`logQueue.ts`), the streak reader (`serverStreaks.ts`) and the client-side text helpers of the old path are deleted, together with their tests. The beat controller no longer turns beats into lines: it runs the presentation bindings of each client event and posts their lines straight to the chat store (`appendLocalLog`), so a line no longer waits for a card landing and no line is dropped by a flush. The repeat guards (same direction, a placed seat, the winner again, a repeated envelope) now live in the replay only, which still feeds the bindings for a late joiner and posts their lines alone. `resolveLogText` and its colour and card-name helpers move to `frontend/src/lib/presentation/logText.ts`; the `EventContext` type joins `LogLine` in `logEvent.ts`, because the beat map still carries the streak context. The tests that drove beats only to see a core log line are gone; the copy, the golden and the determinism tests now read the shipped bindings under `mods/uni/presentations/`. The `auto_play_wild` binding gains the `kind` param of the cause card, so an auto-played wild names its card instead of showing a raw placeholder, and only the `defs` and `match_start` content digests in the event stream goldens move.
- **Client stack-penalty play-log guess** — the client no longer guesses the stack line from a +2 or +4 play and its debt toast. The `draw_stacking` mod now writes that line. The `uni:log.draw_stack` and `uni:log.draw_stack_heavy` texts are gone from the core texts. Non-English texts for the stack line stay out until the `draw_stacking` locale follow-up.

## [0.6.2] - 2026-10-17

### Fixed

- **The cards of an opponent can be clicked to pick them as a target.** Only their avatar took the click before, so a click on the fan of cards did nothing. The cards now pick the same seat as the avatar, for a target prompt and for a spectator switching view.
- **A +4 can answer a +2 stack, and a +2 a +4.** The stack gate now checks only that the answering card and the top card both carry the stackable tag, not that they have the same draw value. Before, the client dimmed a +4 held against a +2 stack even though the engine accepted it. A wild answering a stack keeps the previous color, because it has no color of its own until its prompt answers.
- **The "Select a target" banner no longer hides the opponents.** The banner sat over the avatar of the opponent at the top of the table, the part you click to pick a target. It now sits lower on the screen.
- **The draw debt can be taken with one tap on the draw pile.** While a draw-stacking window is open and you hold the debt, the top card of the draw pile pulses white and one tap takes the debt. You no longer wait for the window timer to run out. A jump-in group keeps the offer back until its hold ends.
- **A stack or jump-in response now sets the active color.** The winning response card of a window (a stacked +2/+4, a jump-in) moved to the discard pile but left the active color on the old one, so the table color and the top card disagreed. The response now sets the active color like a normal play; a wild with no color of its own keeps the previous color until its prompt answers.
- **Effects now resolve before the win check.** A 7 or 0 played as the last card used to pass the empty hand on and let the receiver draw. The win check now runs after the effects, so the seat that receives the empty hand wins.

## [0.6.1] - 2026-10-07

### Fixed

- **Toggled rule mods now reach the match.** A production lobby always starts from the classic deck snapshot, and the server added the toggled rules (`draw_stacking`, `seven_zero`, `jump_in` and the rest) to the deck only on the freestyle path. Every toggled rule was therefore dropped before assembly: the mods loaded and validated, but the match played as standard UNI. The server now adds `active_mods` to whichever deck it resolves, so the classic deck and the rules work together. A development lobby has no deck snapshot, which is why the bug never appeared locally.

## [0.6.0] - 2026-10-05

UI Glow Up, 3D Table, 16 Players! The table is now a 3D scene, a match can hold up to 16 players, and nearly every screen around the game got reworked to match.

### Added

- **3D table**: the whole board is a top-down 3D scene. Opponents, your hand, both piles and the playmat are real objects on a table instead of flat page elements. The layout fits any screen shape, from narrow phones to wide monitors.
- **Dealing cinematic**: every match opens with each card flying to its seat and the first discard turning over. Click to skip it.
- **Match preferences**: you can turn off click-to-play if you hate misclicking cards.
- **Scrolling hand**: when your hand outgrows the screen it scrolls instead of squeezing, with the end cards fading out.
- **Colour ripple and dust**: changing colour sends a dithered ripple across the playmat, with dust drifting over the table.
- **Play direction loop**: play direction is shown as a loop of arrows marching around the playmat.
- **Card impact**: skip, reverse, +2, wild and wild +4 hit the table with a short hit-stop, a small camera punch and a flash of the card's colour.
- **Draw-debt badge**: the +N draw badge pops every time the stack grows and changes colour as the debt climbs. The draw pile shakes with it.
- **Your-turn cue**: your seat pulses when it becomes your turn, then keeps a faint glow while the turn is yours.
- **Server-driven animations**: animations come straight from the server, so cards fly where they really went instead of where the client guessed. All of it respects the reduced motion and animation settings.
- **Up to 16 players**: hosts set the player cap per lobby, from 2 to 16, with a new slider in lobby settings. Starting cards are clamped so the deck always covers everyone. The lobby list shows a proportional occupancy gauge instead of one icon per seat.
- **Bottom-edge timer bar**: a bar along the bottom edge of the screen drains to show how long you have left, whether it is your turn clock or another player's move.
- **Loading gate**: matches wait for every player to finish loading before the first turn clock starts, behind a loading screen before the match.
- **Race mode**: a race to empty your hand. The first players to empty their hand finish 1st, 2nd and so on, and win. The host picks how many win, from 5% to 95% of the players. Finished players drop out of turn order and watch. The rest are ranked by fewest cards left.
- **Chat during a match**: the chat button stays available during a match, in the free top-right corner.
- **Play log**: chat gains a play log. Skips, reverses, draw stacks, wild colour picks and near-wins write short contextual lines. Streaks escalate: get skipped three times in a row, or watch the draw stack hit 8, and the log notices. The jokes are written in English and machine-translated for the other languages. Mods cannot add play-log lines yet.
- **Card glossary**: every card played shows up as a keyword you can tap to open its glossary entry. Right-click or long-press a card to see information about it and about anything it mentions.
- **Data-driven engine for modders**: matches run on a data-driven engine. Cards, decks and rules are content that the server assembles, instead of compiled behaviour.

### Changed

- **Palette**: a new red, green, blue and yellow palette, defined once and used everywhere: cards, the colour picker, seat colours and the victory popup.
- **Performance**:
  - A full load of everything but your language is roughly 0.8 MB over the wire, down from about 4.8 MB in 0.5.5, around 80% less.
  - Assets are served compressed, Brotli where the browser supports it and gzip otherwise.
  - Languages load on demand. English is built in and every other language is its own download of about 11 kB.
  - The animated home background starts loading once everything else is done, then fades in.
  - Music streams instead of downloading whole, waits until your first click or key press, and is not fetched at all when the music volume is 0. Sound effects at volume 0 are skipped too.
  - Every image, sound and icon carries a content hash in its name, so the browser keeps it for a year and an update still takes effect immediately.
  - About 3 MB of source art files and about 1 MB of unused 3D decoders no longer ship with the game.
  - These are rough numbers from one browser's network panel, not a benchmark.

### Security

- WebSocket connections from unlisted origins are rejected.
- The server refuses to start with weak or missing secrets.
- Responses carry stricter browser security headers.
- Direct messages are encrypted and tied to their sender and recipient, and the server runs with fewer privileges.
- Fixed several bugs where a single malformed JSON or WebSocket message could crash the server.

### Known issues

- Mods cannot write their own play-log lines yet, so modded cards show up without a reaction.
- The profile screen is rough and still waiting for its own makeover.
- Past four seats, player colours repeat in red, green, blue, yellow order. A proper profile colour is planned.

## [0.5.5] - 2026-07-17

### Added

- **Backend log level threshold and optional file sink**: `Logger` now filters calls against `LOG_LEVEL` (`debug`/`info`/`warn`/`error`/`silent`, defaults to `info`), the noisy per-request `Log`/`WS`/`Lobby`/`HTTP` trace calls are Debug-level and stay quiet unless `LOG_LEVEL=debug` is set. Setting `LOG_FILE` to a path mirrors every printed line there in plain text (no ANSI codes), left unset by default so nothing changes for local dev.
- **Client IP in connection/request logs**: WebSocket connection open/close, the per-action WS trace log, the per-route HTTP trace log, and the upgrade-rejection warnings in `webserver.cpp` now all include `ip=<address>` (already captured on `PerSocketData` for rate limiting), so abusive clients can be identified by grepping `ip=` instead of cross-referencing usernames.

### Changed

- **Main screen login/guest flow no longer forces navigation**: `App.svelte` and `MainScreen.svelte` no longer `goto("lobbies")` after closing the auth modal or playing as guest, the modal overlays whatever screen was already current, so logging in or starting a guest session from Main now leaves you on Main, re-rendered as the logged-in/guest hub instead of jumping to Lobbies.
- **Main screen hub tiles**: `HubTile.action` no longer doubles as the "coming soon" flag, a new `badge?` field carries the caption (e.g. "Soon") independently. The Stats tile now works for guests too, tapping it opens the login modal (`storeNavigation.gotoAuth("login")`) instead of sitting inert; Decks and Skins keep an explicit "Soon" badge.
- **Main screen hero layout**: the logo and welcome-back line are now centered in the space actually free above the dock, measured via `dockHeight` (`bind:clientHeight`) instead of bottom-anchored with `flex-end`, which left a growing empty gap up top the taller the dock got. The dock itself is now `fixed` to the bottom instead of in-flow. A new dither-radial halo image (`dither-radial.png`) sits behind the logo, replacing the page-fixed radial cutout baked into `bg_main.png`, which didn't track a re-centered logo.
- **Lobby leave is now optimistic**: `StoreLobby.leaveLobby()` clears local lobby state and navigates back to Lobbies immediately instead of waiting for the server's `LobbyLeft` echo, an action that's virtually never rejected shouldn't stall the UI on round-trip latency.

### Fixed

- **WebSocket duplicate-reconnect race**: `WebSocketClient` could open a second, uncoordinated socket under the same identity when a `disconnect()`→`connect()` pair raced against that first socket's own `onclose` (e.g. re-upgrading identity on login), or when a pending auto-retry from a previous drop survived an explicit `connect()`. Both orphaned a socket that stayed subscribed to broadcast topics, causing duplicate chat messages per login/logout cycle. Sockets closed via `disconnect()` are now tracked in an `intentionallyClosedSockets` WeakSet so their own `onclose` never schedules a reconnect, `connect()` now clears any pending reconnect timer before opening a new socket, and both reconnect paths now go through `connect()` (which already dedupes concurrent attempts) instead of `_connectOnce()` directly.
- **Guests and bots no longer accumulate player stats**: `match_instance.cpp` now checks for a matching `users` row before inserting into `player_stats`, guests and bots (who have none) are skipped entirely instead of showing up in the leaderboard under a throwaway username nobody can look back up.

## [0.5.4] - 2026-07-16

### Changed

- **Chat log, own messages labeled "You"**: `ChatLog.svelte` shows "You" instead of your own username on lines you sent, other authors still show their username as before.
- **Chat log, plain lines instead of pixel-bordered bubbles**: individual chat lines dropped the `.pixel-bordered` notch treatment added in 0.5.2, a border per line read as noisy in a scrolling log.

### Fixed

- **Guest identity stuck after logging into a real account**: logging in, starting a guest session, or logging out all rotate the `ws_token` cookie the WebSocket upgrade authenticates with, but an already-open socket kept whatever identity it upgraded with until it reconnected on its own. Sending chat right after logging in (without a page reload) still posted under the old guest name. `auth.svelte.ts` now forces the socket to reconnect after each of the three so it re-upgrades under the new identity right away.
- **Frontend dev rebuilds not reaching the running server**: `sync_public`'s `copy_directory` step only refreshed `build/Release/public` on the next `cmake --build`, so a `npm run watch` rebuild (which writes straight into the root `public/`) needed a manual server restart to show up. `build/Release/public` is now a symlink to the root `public/` instead of a copy, always current. The server also pre-loads every static file into memory at startup for fast repeat serving in production; a new `STATIC_CACHE` env var (default on) can be set to `0` in dev so it reads straight off disk instead, no restart needed either way.
- **`LobbyController::RemoveMember` no longer walks socket internals for a departing member's own leave**: the explicit-leave path (a player hitting "leave lobby" while still connected) already has the leaver's `PerSocketData` from the request itself, `RemoveMember` now takes it directly instead of re-deriving it via `socket->getUserData()`, needed only for removals acting on someone else's socket (kick, eviction).

## [0.5.3] - 2026-07-16

### Added

- **Match pacing and completion analytics**: `match_end` now carries `time_to_play_avg_ms`, the average time a human player took per turn during the match (bot turns are excluded so their near-instant plays don't pull the average down), and `winner_is_bot`. `game.svelte.ts` tracks each turn's start time in `MatchStateUpdated` and accumulates human-only turn durations for the current match.
- **`match_saved` event**: a mid-match quit with `save_state` on ends the match without a winner but keeps its state, and now reports its own `match_saved` event (`duration_seconds`) instead of being indistinguishable from a real completion. A true abort with no save (`quit_deletes_match`, no save) fires neither `match_end` nor `match_saved`, so the abandon rate can be read off as started minus completed minus saved.
- **`match_settings` event**: the full ruleset (mods, starting cards, turn time limit, bot settings, save/quit behavior, public/private) is now its own event with flat parameters, fired alongside `match_end`/`match_saved`, instead of a single `settings_json` string that GA4 couldn't break into separate dimensions.
- **`account_type` dimension on `screen_view`**: reports `registered`, `guest`, or `anonymous` depending on the current session, so real accounts can be told apart from guest sessions and pre-auth traffic in GA4. No GA4 User-ID or other persistent identity is set, this is a coarse bucket only.

### Changed

- **`match_start` is minimal now**: it reports only `player_count`, `mod_count` and `is_public`. A match starting says nothing about whether it ever finishes, so the full ruleset moved to `match_settings` and is reported once the match actually ends.

### Removed

- **`play_card`, `draw_card` and `call_uno` analytics events**: fired on every single move with no parameters attached, they added volume without telling us anything. Dropped entirely, with no replacement.
- **Ad components**: `AdBanner.svelte` and `AdInterstitial.svelte`, and the interstitial that used to show at the end of a match in `GameScreen.svelte`, are gone. Ads are coming back once there's an actual ad strategy behind them; the site-ownership meta tag and the AdSense loader script stay in `index.html`.
- **"UNO!" calling**: the button, the `has_called_uno` state, the draw-2 penalty for forgetting, and the `match_call_uno` WS message are all gone. The rule only pays off around a physical table with other people watching for the slip-up; on a screen, clicking a button adds friction without adding fun. It was also a copyright liability not worth carrying for a mechanic nobody would miss.

## [0.5.2] - 2026-07-11

The chat dock talks to the server now: the mocked fixture data from 0.5.0 is
gone, replaced by real `chat_send`/`chat_message`/friends traffic, DM
history, and a shard-paginated global chat history so joining players see
recent conversation instead of an empty room.

### Added

- **Global chat history on join**: `ChatController::OnOpen` pushes a shard of recent global chat (`chat_history`, `channel: "global"`) to every newly connected socket, so late joiners see recent conversation instead of an empty room. Gated by `CHAT_GLOBAL_HISTORY_ON_JOIN` (default on) and sized by `CHAT_GLOBAL_HISTORY_SIZE` (default 64, `-1` sends the entire 200-message in-memory ring buffer as one shard).
- **Cursor-paginated chat history**: `chat_history_request`/`chat_history` now shard both global and DM history instead of returning it all at once, each message carries a monotonic `id`, and `chat_history_request` accepts `before_id`/`limit` to walk further back. `ChatService::GetGlobalHistoryPage`/`GetDirectHistoryPage` do the windowing; the server always clamps client-requested `limit` to `[1, 100]` (default 20) regardless of what's asked, so a client can never force a multi-MB single response. The frontend's `ChatLog.svelte` shows a "Load older messages" button once `chatStore.hasMoreHistory()` is true, wired through `chatStore.loadMoreHistory()`.
- **Chat wired to real WebSocket traffic**: `chat.svelte.ts` no longer serves fixture data, `send()` emits `chat_send` (mapping the UI's `global`/`party`/DM channels to the backend's `global`/`lobby`/`dm`), incoming `chat_message` frames are routed to the right thread (DM routing resolves the thread partner from `target` vs `username` depending on who sent it), and DM threads hydrate their first history shard the first time they're opened.
- **Party chat gated to being in a lobby**: the party tab is disabled (with a tooltip) and sending is blocked with an inline composer error when the socket has no `lobby_code`; `ChatLog` shows a "Join a lobby to use party chat" empty state instead.
- **Friends, for real**: `friend_list_request`/`friend_list`/`friend_request`/`friend_response` replace the mocked friends list. `FriendsList.svelte` gained an "Add friend by username" field and accept/reject buttons for incoming requests; unknown/duplicate/missing-request errors get real messages in `errors.ts`.
- **Inline composer errors**: fire-and-forget actions like `chat_send` have no `emitAndWait` caller to report to, so their `error` frames are now surfaced as a transient message under the composer (scoped to while the dock is open, cleared after 5s or on the next send).
- **Lobby/party chat history parity**: `chat_history_request` now handles `channel: "lobby"` the same way as `"global"`/`"dm"`, `ChatService::GetLobbyHistoryPage`/`PostLobbyMessage` shard and store party chat per lobby, and `ClearLobbyHistory` drops it once the lobby is destroyed (wired via `ILobbyStore::OnLobbyDestroyed`). The frontend's `loadMoreHistory()`/`#hydratePartyHistory()` now work for party chat exactly like global/DM.
- **`CHAT_HISTORY_SHARD_SIZE` env var**: the shard size used when a `chat_history_request` omits `limit` is now configurable (default 50, server-clamped to 100 regardless). Documented in `.env.example`.
- **Pixel-bordered chat bubbles**: individual chat lines in `ChatLog.svelte` now get the `.pixel-bordered` notch treatment instead of a plain `<p>`.

### Changed

- **Lobby, redesigned as a dealt hand of cards**: `LobbyScreen.svelte`'s member list is gone, each seat now renders as an actual playing card (`background.png`/`border.png`, tinted per-seat in one of the four UNO card colours instead of an arbitrary avatar palette), with the host's crown living directly on the avatar so future cosmetics can stack the same way, instead of a separate corner badge. Kick/promote is triggered by right-click on desktop or a tap on touch devices (tracked via pointer type on the seat itself) instead of an always-visible kebab button. Open seats render as a muted, valueless version of that seat's card (same background/border assets, desaturated) with a pulsing "Waiting…" label, instead of the branded card-back.
- **Lobby settings, in a modal**: `LobbySettings` no longer sits in an always-visible side panel, it opens from a "Settings" button next to the saved-matches list, in the same `Modal` component used by Browse's create/join dialog, and no longer carries its own bordered `.panel` box (which doubled up with the modal's own border/padding).
- **Lobby mobile layout**: seats lay out as a 2×2 grid below desktop width and a single row on desktop instead of wrapping unevenly (3-then-1), the title shrinks below desktop width to stay legible (matching Browse's title treatment), and the empty-seat pulse animation anchors to wall-clock time (negative `animation-delay`) so a seat vacated mid-pulse doesn't restart out of sync with the others.
- **Lobby browse, clearer empty states**: "no lobbies are currently open" and "no lobbies match your filters" are now distinct states, each with its own retry/clear-filters action, instead of one generic empty message.
- **Auth as a modal**: logging in or registering no longer navigates to a dedicated screen, `AuthScreen` opens as an overlay (`storeNavigation.gotoAuth()`/`closeAuthModal()`) on top of whatever screen you were on, and closes back to it instead of losing your place. The main screen's welcome banner and logout label now distinguish guests ("Playing as") from full accounts ("Welcome back,").
- **Discord added to the social links**: a new Discord icon leads the main screen's social row.

### Fixed

- **Stale build artifacts**: `sync_public` is now its own CMake target (previously a `uni_server` post-build step), so frontend asset changes reach the runtime directory even when the C++ binary has nothing to relink; Vite's `--watch` build now prunes orphaned hashed JS/CSS chunks on every rebuild via a new `pruneStaleChunksPlugin`, instead of leaving stale ones behind.

## [0.5.1] - 2026-09-07

### Added

- **IndexNow key verification file**: `frontend/public/abc4d7786e98443fa16f35efe0333950.txt` lets search engines confirm ownership before accepting IndexNow indexing notifications.

## [0.5.0] - 2026-07-07

The "half-UI" update: a ground-up redesign of the landing and lobby-browse
screens, first-class guest play, and a new audio layer. The browse, settings,
guest, censor and audio subsystems are all introduced here, so the entries
below describe what each one _is_, not the in-development bugs closed while
building it.

### Added

- **Settings screen**: `frontend/src/lib/components/settings/SettingsScreen.svelte` wires the main-screen "Settings" hub tile to a real screen with music/SFX volume sliders (reusing `lobby/settings/Slider.svelte`), bound to `storeAudio`. It's local browser state, so guests can reach it too, no account needed.
- **Guest sessions**: New `POST /auth/guest` issues an ephemeral session: a generated `Guest#XXXXX` display name (the `#` is outside the registration username pattern, so guests can never collide with real accounts) and a `ws_token` cookie, but no `auth_token`, so `/auth/me` still reports logged-out and guests are never mistaken for full accounts. Guests can browse, join and play; the main-screen button requests the session before navigating, and stats and saved matches remain account-only. The main-screen hub-tile dock (Stats/Decks/Skins/Settings) shows for guests as well as members (`isLoggedIn || isGuest`), with `Stats` gated to real accounts and `Decks`/`Skins` as placeholders for everyone.
- **Server-driven rule catalog (`metadata_request` WS action)**: the rule catalog (`available_rules`) is fetched lazily once per session via a new action (`LobbyController::HandleGetMetadata`) instead of being attached to every join/create/rejoin response. A new frontend `storeCatalog` caches it with a shared in-flight promise so concurrent consumers (Browse filters, LobbySettings) trigger a single request. Rule labels/descriptions come from this server catalog instead of a hand-synced `RuleId` union, so new rules appear automatically. The contract's `Metadata`/`MetadataRequest` messages are extensible for future deck/card catalogs, and the `MaxLobbyMembers` x-constants annotation (lost in an earlier contract rework) is restored as the `LobbyMemberCap` schema, so `MAX_LOBBY_MEMBERS` is generated again instead of hardcoded 4s.
- **Browse view-model, extracted and unit-tested**: pure helpers (`toBrowseLobby`, `filterLobbies`, `sortLobbies`, `joinInfo`, `openSlots`, `category`, `avatarColor`) live in `lib/utils/lobbyBrowse.ts` with a Vitest suite (26 tests) covering payload parsing, every filter, sort order and the join-button matrix. Client presentation catalogs (rule icons, future i18n label overrides, mocked decks, avatar palette, sort options) live in `lib/data/lobbyCatalogs.ts`.
- **Multi-language profanity censor**: `lib/utils/censor.svelte.ts` masks profanity client-side (display-only, not a moderation layer) across 28 languages, sourced from the LDNOOBW word lists (`lib/data/profanityWords.ts`, credited on `/credits.html`) and dynamically imported so the ~2300-word dataset only loads once the register screen is reached, not bundled into the main chunk. Matching is plain substring (not word-boundary-checked): a deliberate tradeoff so that glued-together abuse (`fuckyou99`, repeated slurs) is always caught, at the cost of occasionally masking inside innocent words. Wired into `RegisterForm.svelte` (blocks registering a profane username). Disable/custom-word-list hooks exist (`setCensorEnabled`, `addCustomWords`) but aren't wired to any settings UI yet.
- **Swipe-to-dismiss toasts**: the per-toast rendering (accent bar, icon, countdown) moved out of `Toast.svelte` into a new `ToastItem.svelte`, which supports dragging a toast horizontally (via `svelte-gestures`' `usePan`) past an 80px threshold to dismiss it early, with the toast snapping back if released short of that.
- **Background music and SFX manager**: built on Howler.js (`lib/audio/musicPlayer.ts`, `lib/audio/sfxPlayer.ts`) with a raw-Web-Audio engine (`lib/audio/multiChannelSync.ts`) for sample-accurate multi-channel stem playback that Howler alone can't do. A real track (`music.fuzzsong`, by birdrun) plays on every screen except the match itself; volume settings persist to `localStorage`. 23 gameplay/lobby trigger points (card play/draw, UNO call, victory/interrupted popups, draw-stack, lobby join/leave/kick/promote/start) are wired to `storeAudio.playSfx(...)` with placeholder catalog ids, `SFX_CATALOG` is intentionally empty until real sound files are sourced; every call site is tagged `// PLACEHOLDER-SFX:` for later follow-up.

### Changed

- **New landing / main-screen UI**: redesigned to be responsive and mobile-friendly (the previous UI filled the screen with "UNI" and no visible buttons on mobile). Hub tiles mark "coming soon" via an absent `action` instead of no-op callbacks, the social links are data-driven (one array, one loop) instead of six copy-pasted anchors, and the lobby-card rule-overflow estimation uses named constants in place of magic pixel numbers.
- **New Lobby Browse UI**: the 730-line screen is now a thin shell (state + layout) over reusable components, `common/ToggleChip` (the pixel-bordered on/off chip previously copy-pasted ~12×), `common/Listbox` (generic accessible dropdown with a state-driven roving focus index, Home/End support and outside-click dismissal via a shared `use:clickOutside` action, replacing the bespoke sort dropdown whose keyboard nav queried the DOM), `lobby/PlayerSlotRow` (the three near-identical avatar loops + sort preview), `lobby/LobbyCard`, `lobby/AdvancedSearchModal` and `lobby/BrowseToolbar`, all with `aria-label`s. Rule filters are driven by the server catalog. Empty / error / "no lobbies match your filters" states float directly on the page instead of sitting inside a dashed-border card; loading shows the `LoadingSpinner` ring instead of text; an error-illustration slot is wired up (`ERROR_ILLUSTRATIONS`) pending real art. The toolbar highlights the search field's pixel border on focus (instead of the browser ring on the inner input), drops the duplicate Logout button (it lives on the main screen), and uses the tiny UI font for small filter/sort controls while reserving the pixel font for the big Create / Advanced / Back / Clear CTAs.
- **Lobby store and WebSocket client hardening**: incoming lobby payloads (`LobbyJoined`, `LobbyUpdated`, `LobbyRejoin` response, `LobbyList`) are Zod-validated at the WS boundary, malformed frames are logged and dropped instead of corrupting store state. Browse counts are derived from the `members` list (humans = non-bot members) with guards for missing fields, the list `status` tracks full/open live from member count on every update, and Browse polls the list every 10s while open (with an overlap guard) since the server only pushes updates to lobby members. `create()` catches network errors like `join()` did; `updateSettings()` and the saved-matches fetch surface errors as toasts instead of failing silently; the duplicate `MatchStateUpdated` race between the join handler and the rejoin flow is replaced by a shared one-shot redirect guard; a stray broadcast can no longer overwrite `current` with a different lobby (invite-code guard); `leave()` reconnects before emitting so the frame isn't dropped on a dead socket; concurrent `ws.connect()` calls share one in-flight attempt instead of opening a second socket; non-JSON frames and throwing handlers no longer kill the dispatch loop; the join form uses the store's real `isLoadingJoin`; the create form keeps the typed name when creation fails; and a `storeLobby.listError` flag distinguishes a failed fetch from "no lobbies" so Browse shows a "Couldn't load lobbies" retry card (the failure toast fires only on the transition into failure, not on every 10s retry).
- **Readable connection errors**: a failed WebSocket connection now rejects with a proper `Error` instead of the raw browser `Event`, so toasts read a real message instead of "ERROR! [object Event]"; a new `failureText()` helper guards every catch-toast against non-Error rejections. The toast accent bar is now the toast's actual full-bleed left edge (notched by its own pixel-corners clip) instead of a small inset rectangle floating inside the border.
- **Random avatar colours**: `PlayerSlotRow` colours are now picked fresh from `AVATAR_COLORS` on every render (the old `avatarColor()`, now removed, derived them deterministically from the invite code, so a lobby always showed the same "random-looking" colours). Purely decorative, no identity to preserve.
- **Import path aliases**: `$components`, `$stores`, `$utils` and `$data` join the existing `$lib` catch-all (`vite.config.js`, `tsconfig.json`), and every relative `../`/`../../` import across the frontend now uses the most specific one instead.

## [0.4.8] - 2026-07-07

### Fixed

- **Lobby store and WebSocket client hardening**: incoming lobby payloads (`LobbyJoined`, `LobbyUpdated`, `LobbyRejoin` response, `LobbyList`) are now Zod-validated at the WS boundary, malformed frames are logged and dropped instead of corrupting store state; the browse list's `status` now tracks full/open live from the member count on every lobby update ("in-game" still comes from the list fetch); `create()` catches network errors like `join()` already did and both now return a success flag; `updateSettings()` and the saved-matches fetch surface errors as toasts instead of failing silently; the duplicate `MatchStateUpdated` race between the join handler and the rejoin flow is replaced by a single shared one-shot redirect guard; a stray broadcast can no longer overwrite `current` with a different lobby (invite-code guard); `leave()` reconnects before emitting so the frame isn't silently dropped on a dead socket; `Lobby.member_count` (never actually sent by the server on updates) was removed in favour of deriving counts from `members`; concurrent `ws.connect()` calls now share one in-flight attempt instead of opening a second socket; non-JSON frames and throwing message handlers no longer kill the dispatch loop; the join form's dead local loading flag now uses the store's real `isLoadingJoin`; the create form keeps the typed name when creation fails; `MAX_LOBBY_MEMBERS` from the generated contract replaces hardcoded 4s.
- **Readable connection-error toasts**: a failed WebSocket connection used to reject with the raw browser `Event`, so toasts printed "ERROR! [object Event]". The client now rejects with a proper `Error` and a new `failureText()` helper guards every catch-toast against non-Error rejections.
- **Toast accent bar**: the coloured status strip is now the toast's actual left edge (full-bleed, notched by the toast's own pixel-corners clip) instead of a small inset rectangle floating inside the border.
- **Lobby browse liveness**: the server only pushes lobby updates to lobby members, so the Browse screen now polls the list every 8s while open (with an overlap guard), new, closed, filled and started lobbies finally show up without re-entering the screen.
- **Lobby browse crash after leaving a lobby**: the `LobbyUpdated` handler copied `member_count`/`bot_count` straight from the broadcast payload into the public lobby list, but `BroadcastUpdate` only sends the `members` array, both fields came back `undefined`, slot math produced `NaN`, and `{#each Array(NaN)}` threw `RangeError: invalid array length`, blanking the Browse screen. Counts are now derived from the `members` list (humans = non-bot members), and the Browse mapping guards against missing counts.

### Changed

- **Main screen cleanup**: hub tiles now mark "coming soon" via an absent `action` instead of no-op callbacks, and the social links are data-driven (one array, one loop) instead of six copy-pasted anchors. The lobby-card rule-overflow estimation got named constants in place of magic pixel numbers.
- **Lobby browse toolbar polish**: the search field highlights its pixel border on focus (instead of the browser focus ring on the inner input); the duplicate Logout button was removed (it lives on the main screen); and the small filter/sort controls (search text, quick toggles, advanced-search chips) use the tiny UI font, reserving the pixel font for the big Create / Advanced / Back / Clear CTAs.
- **Production deploys now gated on release tags**: CI still builds a multi-arch image on every push to `main`, but tags it `:edge` (+ `:sha-*`) instead of `:latest`. Only pushing a `vX.Y.Z` git tag publishes `:latest` (plus the matching semver tag), which is what the OCI deploy-watcher polls, so `main` can accumulate unreleased work without it shipping to playuni.app, and a release is cut simply by tagging.
- **Canonical domain moved to `playuni.app`**: All canonical/OG/structured-data URLs, `sitemap.xml`, `robots.txt`, and the GA4 `cookie_domain` now point at `https://playuni.app` instead of `https://unii.duckdns.org`. Traefik (in `uni-infra`) serves the new apex as primary and 301-redirects `www.playuni.app` and the legacy DuckDNS host to it, keeping `/.well-known/` answering 200 on the legacy host so the existing Bluesky atproto handle verification keeps resolving until the handle is re-verified against the new domain.
- **Import path aliases**: `$components`, `$stores`, `$utils` and `$data` join the existing `$lib` catch-all (`vite.config.js`, `tsconfig.json`), and every relative `../`/`../../` import across the frontend now uses the most specific one instead.

### Added

- **Guest sessions**: "Play as Guest" was a dead end, it navigated to the lobby list but the WebSocket upgrade rejected the connection for lack of a `ws_token`, so account-less players could never see or join a lobby. New `POST /auth/guest` issues an ephemeral session: a generated `Guest#XXXXX` display name (the `#` is outside the registration username pattern, so guests can never collide with real accounts) and a `ws_token` cookie, but no `auth_token`, so `/auth/me` still reports logged-out and guests are never mistaken for full accounts. The main-screen button now requests the session before navigating; stats and saved matches remain account-only. Frontend rule-icon map also re-keyed to the real backend rule ids (`progressive`, `no_bluffing`, `jump_in`, `force_play`, `draw_stacking`, `seven_zero`), the old hardcoded catalog had drifted from the server.
- **Dedicated `metadata_request` WS action**: the rule catalog (`available_rules`) is no longer attached to every join/create/rejoin response, the client fetches it lazily once per session via the new action (`LobbyController::HandleGetMetadata`), and a new `storeCatalog` (frontend) caches it with a shared in-flight promise so concurrent consumers (Browse filters, LobbySettings) trigger a single request. `storeLobby.availableRules` is gone; the contract's `Metadata`/`MetadataRequest` messages are extensible for future deck/card catalogs. The `MaxLobbyMembers` x-constants annotation (lost in an earlier contract rework) is restored as the `LobbyMemberCap` constraint schema, so `MAX_LOBBY_MEMBERS` is generated again instead of hardcoded 4s.
- **Browse view-model extracted and unit-tested**: pure helpers (`toBrowseLobby`, `filterLobbies`, `sortLobbies`, `joinInfo`, `openSlots`, `category`, `avatarColor`) moved from `LobbyBrowse.svelte` into `lib/utils/lobbyBrowse.ts` with a Vitest suite (26 tests) covering the NaN-crash payload class, every filter, sort order and the join-button matrix. Client presentation catalogs (rule icons, future i18n label overrides, mocked decks, avatar palette, sort options) live in `lib/data/lobbyCatalogs.ts`; rule labels/descriptions now come from the server catalog instead of a hand-synced `RuleId` union.
- **LobbyBrowse split into components**: the 730-line screen is now a thin shell (state + layout) over new components, `common/ToggleChip` (the pixel-bordered on/off chip previously copy-pasted ~12×), `common/Listbox` (generic accessible dropdown with a state-driven roving focus index, Home/End support and outside-click dismissal via a shared `use:clickOutside` action, replaces the bespoke sort dropdown whose keyboard nav queried the DOM), `lobby/PlayerSlotRow` (the three near-identical avatar loops + sort preview), `lobby/LobbyCard`, `lobby/AdvancedSearchModal` and `lobby/BrowseToolbar`. The search input also gained an `aria-label`.
- **Bluesky handle verification file**: `frontend/public/.well-known/atproto-did` serves the account DID so the Bluesky handle can be domain-verified.
- **Global chat dock (frontend mockup, no backend wiring yet)**: an always-available Minecraft-log-style panel (`ChatDock`) reachable from any screen except `game`, with `[GLOBAL]`/`[PARTY]`/`[FRIENDS]` tabs (`ChatChannelTabs`), a friend list (`FriendsList`), a flat scrolling log (`ChatLog`) and a composer with bold/italic toolbar buttons (`ChatComposer`). Backed entirely by `lib/data/chatMock.ts` fixtures and a Svelte 5 rune store (`chat.svelte.ts`), isolated behind that one file so swapping in real `ws.emit`/`ws.on` traffic later is a single-file change. Per-channel drafts persist to `localStorage`; unread counts track per channel. New `lib/utils/richText.ts` (stack-based tokenizer) plus `common/RichText.svelte` add `**bold**`/`*italic*` markup support, with `TextEffects.svelte` gaining a generic `color` prop.
- **Multi-language profanity censor**: `lib/utils/censor.svelte.ts` masks profanity client-side (display-only, not a moderation layer) across 28 languages, sourced from the LDNOOBW word lists (`lib/data/profanityWords.ts`, credited on `/credits.html`) and dynamically imported so the ~2300-word dataset only loads once a `RichText` instance mounts or the register screen is reached, not bundled into the main chunk. Matching is plain substring (not word-boundary-checked): a deliberate tradeoff so that glued-together abuse (`fuckyou99`, repeated slurs) is always caught, at the cost of occasionally masking inside innocent words. Wired into `RichText.svelte` (chat/rich text rendering), `RegisterForm.svelte` (blocks registering a profane username) and `ChatLog.svelte` (masks displayed usernames). Disable/custom-word-list hooks exist (`setCensorEnabled`, `addCustomWords`) but aren't wired to any settings UI yet.

### Removed

- **Spectate / Take Over buttons**: spectating isn't a feature yet, and "Take Over" was just a join. In-game lobbies now show a plain **Join** button when joinable by replacing a bot, and no button at all otherwise.

## [0.4.7] - 2026-06-28

### Added

- **Gameplay analytics events (GA4)**: A new `storeAnalytics` wrapper (`frontend/src/lib/stores/analytics.svelte.ts`) emits `gtag` custom events from the existing GA4 property. Instrumented points: `screen_view` (navigation), `sign_up`/`login`/`logout`/`auth_error` (auth), `lobby_create`/`lobby_join`/`lobby_leave`, `match_start` (host-only, carries the lobby ruleset, `active_mods`, `starting_cards`, `turn_time_limit_ms`, bot settings, as both broken-out params and a `settings_json` snapshot, excluding the unused `count_*` deck fields), `match_end` (host-only, `duration_seconds`), in-game `play_card`/`draw_card`/`call_uno`, `ws_reconnect`, and `server_error`. Both `match_start` (host's `startMatch()`) and `match_end` (host check in the `MatchOver` handler) fire only on the host, so match, rule-usage, and outcome counts are not inflated by per-client duplication (`MatchOver` broadcasts to every client). The wrapper no-ops safely when `gtag` is absent (ad blockers, SSR). Collected data describes gameplay only, no message content or personal data, and is used solely for game research.

### Fixed

- **Sitemap 404 on HEAD requests**: The server now registers an HTTP HEAD handler for static files alongside the existing GET handler. uWebSockets does not derive HEAD from GET automatically, so `curl -I` (and Google Search Console's sitemap validator) was receiving a 404 even though the file existed. The sitemap is also renamed from `sitemap-index.xml` to `sitemap.xml`, `robots.txt` updated to match, and `.xml` files are now served with `Content-Type: application/xml`.

### Changed

- **Icons migrated to HackerNoon Pixel Icon Library**: The remaining JetBrains Mono Nerd Font (MDI) glyphs across `GameUnoButton`, `FormInput`, `Toast`, `StatsScreen`, `DetailedStatsScreen`, `LobbyScreen`, and `LobbySettings` are now rendered as `<i class="hn pix hn-…">` pixel icons, matching the convention already used in the lobby browse UI.
- **`--mono` font stack**: Dropped the bundled `JetBrainsMono.woff2` (and its `@font-face`) now that it no longer provides icon glyphs. `--mono` resolves to a standard cross-platform monospace stack (`ui-monospace, "SF Mono", Menlo, Consolas, "DejaVu Sans Mono", "Liberation Mono", monospace`).
- **Webfont cleanup**: Removed the unused `MonoPixel` webfont and the `mono` element rule that referenced it. Pixel webfont URLs now carry `?v=` cache-busting query strings so updated glyph files reach returning players.

- **Lobby-not-found copy**: Joining with an invite code that matches no lobby now reads "This code has no lobby associated." instead of the ambiguous "That lobby no longer exists."
- **Main-menu label**: The stray Italian "Entra in Stanza" button is now "Browse Lobbies".

### Removed

- **Lobby connection-status icon**: The per-player connected/disconnected glyph is gone; disconnection will be conveyed by morphing the player's avatar instead. The dead `.nf-icon` optical-centering rule was also removed.
- **Unused assets**: Deleted the Vite starter assets (`hero.png`, `svelte.svg`, `vite.svg`) and the unreferenced `icons.svg` social sprite.

## [0.4.6] - 2026-06-28

### Fixed

- **Google Analytics cookies rejected**: `gtag` config now sets `cookie_domain: 'unii.duckdns.org'` explicitly. `duckdns.org` is a public suffix so browsers blocked cookies scoped to the parent domain.
- **Stats screen in cross-origin embed**: `/stats/me` now accepts `ws_token` (`SameSite=None`) as a fallback so the card arsenal loads correctly inside itch.io iframes.
- **Card arsenal asset 404s**: colour-aggregate cards now use `border.png` instead of an empty path; the Jolly +4 card now correctly maps to `jolly_draw4.png`.

## [0.4.5] - 2026-06-27

### Fixed

- **Disconnected display race**: `OnClose` now guards `member.socket == ws` before marking a player disconnected, preventing a delayed TCP close from a stale socket from stomping the `is_connected=true` state set by a newer connection.
- **No-code lobby limbo**: `OnOpen` now subscribes the reconnecting socket to the lobby pub/sub topic and pushes a full `kLobbyJoined` payload (plus live `kMatchStateUpdated` if a match is active), so a session that lost its invite code is brought back into the correct screen without manual rejoin.
- **Reconnect mid-input**: The `kMatchStateUpdated` push on reconnect now includes `action_required` and `action_context` when the engine is awaiting player input (Wild colour pick, draw-stack confirmation), preventing the input modal from staying hidden after reconnect.
- **Double `LobbyJoined` on rejoin**: When `lobby_code` is stored in `localStorage`, both the server-proactive `OnOpen` push and the `HandleRejoin` response fired the `LobbyJoined` handler, causing triple state writes, duplicate fetches, and stacked `MatchStateUpdated` listeners. The handler now deduplicates by lobby code; `#tryRejoin` skips its own state setup when the event handler already populated `this.current`.
- **Listener leak in `#tryRejoin`**: The `MatchStateUpdated` listener registered during rejoin now has a 1 s timeout so it cleans itself up when no active match follows, and is explicitly cancelled on error paths.
- **DB schema migration v2**: Folds the `cards_played_jolly` column rename together with the users table restructure (`password_hash` → `pass_hash`, new `salt` and `email` columns) into a single migration step. Fixes 500 errors on login/register when upgrading from an older build.
- **Jolly colour flash**: The playmat and arrow tint now hold the last known non-white colour while the colour-pick action is pending, instead of flashing rebeccapurple.
- **Game screen wipe on bot win**: `MatchOver` now clears `actionRequired`/`actionContext` on the client, so the game HUD stays visible (it was hidden by the `{#if !actionRequired}` guard while the popup was showing).
- **Action input stall**: A dedicated per-action timer fires in all bot modes when `IsWaitingForInput()` is true, keyed on `GetPendingPlayer()`. Colour picks and other mid-turn inputs are now auto-handled by a bot after the turn time limit expires, previously this stalled indefinitely in `kPlayInstantly` lobbies.

## [0.4.4] - 2026-06-26

### Changed

- **Wild → White/Jolly**: Wild cards renamed throughout, `Type::kWhite` (was `kWild`), `Value::kJolly` / `kJollyDraw4` (was `kWild` / `kWildDraw4`). Assets, CSS variables, and wire protocol updated to match.
- **game → match**: All internal module names renamed, directories (`include/match/`, `src/match/`), C++ symbols (`MatchState`, `MatchController`, `MatchRule`, `namespace match`), and WebSocket actions (`match_play_card`, `match_state_updated`, etc.).
- **DB migration system**: `PRAGMA user_version`-based migration runner replaces ad-hoc schema init. Schema v1 = current tables; v2 = `cards_played_jolly` column rename.
- **Multicast callbacks**: `LobbyController` `OnGameStarted`/`OnPlayerReplaced` now accept multiple subscribers via `std::vector<std::function<...>>`.
- **RNG**: `std::rand()` replaced with `mt19937` for bot delay jitter.
- **Lobby lookup**: `uint32_t lobby_id` added to `PerSocketData` for O(1) in-game lobby lookup; replaces string `lobby_code` hash lookup in hot path.
- **Contract-generated maps**: `TypeMap` and `ValueMap` arrays generated from `asyncapi.yaml` `x-enums` display metadata; hardcoded maps removed from frontend.

## [0.4.3] - 2026-06-26

### Added

- **Google Analytics**: Injected GA4 tracking tags (GTAG) into all HTML pages. _Note: This is a temporary addition for testing purposes to observe how the tracking integrates with our app._

## [0.4.2] - 2026-06-25

### Added

- **AdSense Interstitials**: Implemented full-screen `AdInterstitial` and `AdBanner` components to gently fund the project without intrusive paywalls.
- **`ads.txt`**: Added IAB authorized sellers file for AdSense compliance.

### Changed

- **SEO & Landing Page**: Completely overhauled the landing page with better SEO (`"UNO®-style card game online"`) and a brand new "UNI vs. UNO" hook.
- **How-to-Play**: Streamlined the rules into a punchy, easy-to-read "Rules at a glance" format while teasing the wild custom mechanics.
- **About**: gave the content more of an identity.

### Fixed

- Cross-origin embedding (e.g. itch.io) now works: login and WebSocket
  connections succeed inside a third-party iframe. A secondary `ws_token`
  cookie (`SameSite=None; Secure`) is issued alongside `auth_token`
  (`SameSite=Strict`) on every login. The WS upgrade accepts either cookie so
  embedded contexts can authenticate without relaxing CSRF protection on HTTP
  endpoints.
- Fixed some typescript errors that were not blocking compilation which were previously not noticed

## [0.4.1] - 2026-06-24

### Fixed

- Tooltips are now correctly rendered above the clipping space of their parent.

## [0.4.0] - 2026-06-24

### Changed

- `ws::ClientAction`, `ws::ServerAction`, and `kServerActionStr` are now
  generated from `contract/asyncapi.yaml` by a new `scripts/generate_ws_hpp.py`
  script (CMake target `gen_ws_actions_hpp`), eliminating the hand-maintained
  copies in `include/common/ws.hpp`. The frontend `outgoingSchemas` lookup
  (action string → Zod schema) is likewise generated by
  `frontend/scripts/generate-schemas.js` and exported from
  `frontend/src/lib/generated/schemas.ts`; the hardcoded map in
  `frontend/src/lib/stores/ws.svelte.ts` is removed. Adding a new WebSocket
  action now requires only a contract edit.
- Controller DI (Phase 2A): extracted `IActionRouter`, `IBroadcaster`, and
  `ITimerService` interfaces; `LobbyController` and `GameController` now take
  the three narrow interfaces instead of a `WebServer&`, enabling construction
  without a live uWS loop. `UwsBroadcaster` and `UwsTimerService` wrap the
  production uWS primitives; `FakeBroadcaster` and `FakeTimerService` serve as
  test doubles.
- WS compression enabled by default (`permessage-deflate`, env-gated via
  `WS_COMPRESSION=0` to disable).
- `GetLobbyByCode` hardened to return `nullptr` and purge stale secondary-index
  entries; all ~12 inline `code_to_id_.find` + `lobbies_.at` patterns collapsed
  to single call-sites.
- RNG hoisted to a member (`std::mt19937 rng_`) on `LobbyController` and
  `MatchInstance`, eliminating per-call `random_device` construction.
- Dead static locals in `SyncBots` removed; SyncBots while-loop indentation
  fixed.
- `MatchInstance` deserialization now uses `.value("key", default)` for every
  field so saves from older schemas load gracefully instead of throwing.
- Active lobby count capped via `MAX_LOBBIES` env var (default 200).
- `MatchInstance::Tick` aborts and marks the match finished if `effect_queue`
  exceeds 64 entries.
- `GetRandomBotName` promoted from free function to `LobbyController` member.
- `Router` base class deleted; `ActionRouter` and `HttpRouter` inline the
  non-copy constructor guard directly.
- Effect factories self-register via static initializers in `EffectRegistry`,
  eliminating manual registration calls during `from_json` deserialization.
- `MatchInstance::ExportState` / `from_json` round-trip complete; all effect
  types serialise their state and restore cleanly.

### Fixed

- `RAND_bytes` failure in `auth_controller` now throws instead of silently
  continuing with an uninitialised salt.
- WS subscription dedup: lobby store's `#registerListeners()` moved to the
  constructor with a latch so handlers are not re-registered on every reconnect.
- Navigation first-connect latch: localStorage screen-restoration only runs on
  the first `onOpen` fire.
- `emitAndWait` pending requests are now rejected with `"disconnected"` on
  `onclose` instead of hanging until their individual timeouts.
- Double-submit prevention: `isActionPending` latch added to `storeGame`
  (`playCard`, `drawCard`, `submitInput`), cleared on `GameStateUpdated` or by
  a 3 s safety timer.
- Lobby start-button locked while `isLoadingStart` is true.
- `logout()` now awaits the POST and shows a toast on failure without clearing
  local auth state.
- `updateAvatar()` revokes the previous blob URL before creating a new one.
- Silent returns in `HandleDeleteSavedMatch` and `HandleResumeSavedMatch` now
  send `kLobbyNotFound` to the client.
- Lobby eviction timer callbacks now use `find()` before map access, preventing
  use-after-erase when a lobby is removed between timer schedule and fire.
- `GameStateUpdated` payload validated through a Zod schema in `storeGame`;
  previously a loose cast silently swallowed structural mismatches.
- `App.svelte` WS listeners cleaned up on component destroy. `MainScreen`
  logout button tracks a pending flag to prevent double-submission.

### Added

- Backend doctest suite: `match_instance_test.cpp`, `rules_test.cpp`,
  `serialization_test.cpp`, `lobby_controller_test.cpp`, engine, rules,
  round-trip serialization, and controller-handle tests via fake transport.
- Frontend Vitest harness: `lobby.dedup.test.ts` (WS dedup regression),
  `game.double-submit.test.ts` (action-pending latch), `ws.failfast.test.ts`
  (emitAndWait disconnect rejection).
- `vitest`, `@testing-library/svelte`, `@testing-library/jest-dom`, `jsdom`
  added as frontend dev dependencies; `npm test` script wired.
- Google AdSense meta tag for site ownership.
- Frontend test fixtures: factory helpers for game, lobby, card, and player
  state; `lobby.handles.test.ts` and `game.actions.test.ts` cover store handle
  lifecycle and action dispatch.

### Fixed

- Renamed the sitemap to `sitemap-index.xml` (and repointed `robots.txt`) to
  recover from a stale Google Search Console fetch cache.

## [0.3.0] - 2026-06-23

### Added

- `CHANGELOG.md` adopted as the canonical in-repo history, with a matching
  "update the changelog" step and section in `RELEASING.md`.

### Changed

- Full Tailwind CSS migration: every screen and component restyled on shared
  design tokens and utility classes, with a responsive pass across the auth,
  lobby, game, and stats flows.
- Consolidated sprite masking and title effects into shared components.
- Splash logo matched to the app hero and FatPixel preloaded for faster mobile
  load.
- Normalized all source comments to a house standard (INFO/BUG/FIXME/TODO/
  ERROR/WARN prefixes, 80-column, end-aligned) and removed redundant ones.

### Fixed

- Bot winner avatar now uses the correct `.gif` extension.

## [0.2.1] - 2026-06-22

### Added

- Standalone About, FAQ, and How-To-Play pages, served as static HTML with a
  shared `seo.css` stylesheet.
- Baseline on-page SEO and an OpenGraph/Twitter link preview.
- A PWA web app manifest (`manifest.webmanifest`) and a `sitemap.xml`.

### Changed

- Static-LCP splash with code-splitting and smoother in-game card flight.
- Favicon switched to `favicon.ico`.
- Replaced the oversized OpenGraph `link_image.png` with a lighter version.
- Asset licensing clarified, CC0 scoped to project work, with a trademark
  disclaimer.

### Fixed

- Corrected inaccurate How-To-Play instructions.
- Errors already handled by their awaiter no longer double-toast.

## [0.2.0] - 2026-06-22

### Added

- Host and lobby settings with contract-bound validation, error codes, and a
  structured error envelope (code + detail).
- Env-driven bot timing, a reconnect grace window, and lobby defaults.
- Client-side translation of backend error codes into readable messages.
- SemVer pre-release tag support in `VERSION`, and a `RELEASING.md` guide.

### Changed

- Centralized CSS variables and pixel-corner styling; began the Tailwind
  rollout.
- Data-driven contract generators via `x-constants`; code-based `SendError` and
  a generic `SendSuccess`.

### Fixed

- Host settings clamped to contract bounds on both ends.
- Pixel-perfect rendering and font corrections across the web client.
- Rank colouring applied correctly to tied #1/#2/#3 players.
- Auth inputs given proper autocomplete and name attributes.
- Jollies prompt for a colour rather than a card type.
- Duplicate toasts eliminated.

## [0.1.1] - 2026-06-19

### Added

- Per-card `can_play` flags and the active colour are sent from the server,
  driving Jolly display.

### Changed

- Card model refactor: `color` → `type` across the contract, backend, and
  frontend; integer `Action` enum wired through the protocol.
- The build now reads the version from the `VERSION` file.

### Fixed

- Playable cards and the UNO prompt are now driven by the server `can_play`
  field.
- The `+4` stack penalty is deferred until the colour choice resolves.
- Bot actions broadcast between each step in instant-play mode.
- Kick/promote use `PlayerRef` payloads, and the promote response is awaited.

## [0.1.0] - 2026-06-18

First tagged release, marking the point where semantic versioning was adopted.
The `VERSION` file becomes the single source of truth, read at build time by
both the backend and the frontend. It captures the full game built up to this
point:

### Added

- A 4-player UNO-like game with a pluggable rule engine: draw-stacking,
  jump-in, 7/0, and no-bluffing rules on top of the standard deck.
- Heuristic, opt-in bots with random names to fill empty seats.
- Lobbies with create, join, and browse flows, host-configurable settings,
  kick/promote, and reconnection handling.
- Account registration, login, and logout over JWT, with avatars.
- A statistics screen with detailed per-player breakdowns, plus saveable and
  resumable matches.
- A C++ uWebSockets backend and a Svelte frontend wired together by an
  AsyncAPI schema-driven contract generated across the stack.
- A pixel-art interface with custom fonts, music, and card/board animations.
- SQLite persistence with WAL journaling for safe online backups.
- SEO metadata, OpenGraph/Twitter cards, a sitemap, and Google Search Console
  verification.
- A multi-stage Docker image published to GHCR via CI, with a standardized
  cross-platform CMake/Conan build.

### Security

- Token-bucket rate limiting for HTTP and WebSocket traffic, login throttling
  with lockout, and per-IP connection caps.
- WebSocket payload-size, idle-time, and backpressure bounds, malformed-frame
  guards, and path-traversal protection on static file serving.

[unreleased]: https://github.com/Eldyn/uni/compare/v0.6.2...HEAD
[0.6.2]: https://github.com/Eldyn/uni/compare/v0.6.1...v0.6.2
[0.6.1]: https://github.com/Eldyn/uni/compare/v0.6.0...v0.6.1
[0.6.0]: https://github.com/Eldyn/uni/compare/v0.5.5...v0.6.0
[0.5.0]: https://github.com/Eldyn/uni/compare/v0.4.8...v0.5.0
[0.4.8]: https://github.com/Eldyn/uni/compare/v0.4.7...v0.4.8
[0.4.7]: https://github.com/Eldyn/uni/compare/v0.4.6...v0.4.7
[0.4.6]: https://github.com/Eldyn/uni/compare/v0.4.5...v0.4.6
[0.4.5]: https://github.com/Eldyn/uni/compare/v0.4.4...v0.4.5
[0.4.4]: https://github.com/Eldyn/uni/compare/v0.4.3...v0.4.4
[0.4.3]: https://github.com/Eldyn/uni/compare/v0.4.2...v0.4.3
[0.4.2]: https://github.com/Eldyn/uni/compare/v0.4.1...v0.4.2
[0.4.1]: https://github.com/Eldyn/uni/compare/v0.4.0...v0.4.1
[0.4.0]: https://github.com/Eldyn/uni/compare/v0.3.0...v0.4.0
[0.3.0]: https://github.com/Eldyn/uni/compare/v0.2.1...v0.3.0
[0.2.1]: https://github.com/Eldyn/uni/compare/v0.2.0...v0.2.1
[0.2.0]: https://github.com/Eldyn/uni/compare/v0.1.1...v0.2.0
[0.1.1]: https://github.com/Eldyn/uni/compare/v0.1.0...v0.1.1
[0.1.0]: https://github.com/Eldyn/uni/releases/tag/v0.1.0
