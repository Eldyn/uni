# UNI Phase 1 — Data-Driven Card Engine (ECS Rewrite) — Design Spec

- **Date:** 2026-09-19
- **Status:** Approved design, pre-implementation
- **Supersedes:** the unimplemented "customizability overhaul" plan formerly at
  `~/.claude/plans/dreamy-snacking-mist.md` (file deleted; binding decisions
  carried forward where still relevant) and the "custom rules roadmap" node-DSL
  sketch in its pre-ECS form.
- **Scope:** Backend-first rewrite of the match engine. Frontend touched only
  where the new contract requires (event source swap, response-window UI,
  generic prompt renderer, deck picker).
- **Implementation is expected to be done by agents other than the author.**
  Section 22 defines the work habits those agents must follow. Work packages
  (section 21) are ordered, independently verifiable, and safe to hand to
  separate sessions.

---

## 1. Purpose

Make the underlying card system of UNI:

1. **Dynamic** — new card types creatable without engine code changes.
2. **Resilient** — hostile or degenerate deck/mod content cannot crash or hang
   the server. A thousand-card hand is legal play, not a bug.
3. **Moddable everywhere, hooks everywhere** — every game concept (cards,
   players, piles, visibility, restrictions, timers, win conditions) is
   addressable and mutable by mod data.
4. **Easy to create content for** — cards are pure data; behavior is composed
   from a server-curated vocabulary of effect ops.
5. **Robust** — full semantic validation at load, runtime loop/chain guards,
   deterministic event ordering.
6. **Infra-ready** — the wire contract carries an event stream and generic
   prompt envelope so custom art, sounds, animations (buckets + animation IR),
   and eventually a Lua scripting layer plug in without another breaking
   change.

Phase 1 delivers: data-defined cards, a pluggable card representation schema
with versioning, players able to play decks containing new cards, and an
engine whose vanilla content is itself just data.

---

## 2. Non-goals (explicitly out of scope, phase 1)

- Workshop UI, deck sharing, mod distribution, asset buckets, CDN upload.
- Lua / scripting VM (the op vocabulary is the seam; Lua arrives later and
  will drive the same ops).
- Replay playback system (RNG outcomes are logged so replays are *possible*
  later; the player is not built).
- IndexedDB card-face atlas persistence (in-memory atlas already exists; see
  project memory `mod-atlas-persistence-todo`).
- Full-fidelity cross-restart match resume (the new engine serializes better
  by construction — resolver state is data — but persistence-to-DB remains
  out of scope; the old serialization gaps close as a side effect, verify).
- Stats for modded matches (vanilla-only stat counting, section 19).
- Any DB schema work (section 18 — no database involvement).
- Balance caps on card counts, draw sizes, hand sizes. Guards protect against
  *non-termination and crashes only*, never against "too much chaos".

---

## 3. Glossary

| Term | Meaning |
| --- | --- |
| **Mod** | A JSON package providing card definitions, rule definitions, and mutation definitions. The *only* source of what can exist. |
| **Deck** | A saved `LobbySettings` snapshot: mod list + card multiset + settings. Selecting a deck loads all of it into the lobby at once. A freestyle lobby is an unsaved snapshot. |
| **Entity** | ECS object: a card, a player, a pile, or the match itself. Referenced by generational handle. |
| **Component** | Typed, data-only attribute attached to an entity. All game state lives in components. |
| **System** | The active runtime behavior of one mod: its hook subscriptions + op graphs. Mods "are" systems in ECS terms. |
| **Hook** | Engine lifecycle event (before/after pairs) that systems subscribe to. |
| **Op** | One node of the server-curated effect vocabulary (e.g. `draw_cards`). Card behavior graphs are composed of ops. |
| **Behavior graph** | DAG of ops + branch/fork/join nodes defining what a card or rule does. |
| **Status effect** | A component representing a timed/counted effect on an entity (draw debt, shield, restriction aura, visibility grant). |
| **Response window** | A uniform, timed pause during resolution in which any eligible responder may act before the default outcome applies. |
| **Mutation** | A mod-declared modification of another mod's card/rule behavior: replace, wrap, veto, or filter. |
| **Beat** | Client-side animation unit produced by the animation queue from event packets (existing term from the GSAP migration). |
| **Aspect** | A unit of information visibility: count / color / value / identity / position. |

---

## 4. Decision record (locked; do not re-litigate without the owner)

From the 2026-09-19 brainstorm. Each row is binding.

| # | Decision |
| --- | --- |
| Q1 | Base deck is **pure data**. Engine is interpreter only. No hardcoded cards. Mods read game data the same data-driven way they write it. |
| Q2 | Two-layer identity: **compact indices on the wire** (uint32: mod 8 / kind 12 / instance 12), **stable string IDs everywhere else**. Mutations and decks reference string IDs only. |
| Q3 | **No backward compatibility** required at release. Low userbase; single breaking release accepted. |
| Q4 | **ECS**: card = entity, components = behavior data, mod = system. Hybrid: server holds the canonical op vocabulary; card defs compose ops. Lua later via the same seam. |
| Q5 | Full hook catalog with **before and after** variants, plus visibility hooks (opponent hand colors/values/identities, deck position). Catalog extensible in later versions. |
| Q6 | Mutations support **replace, wrap, veto, and filter**. Conflict detection at load; phase 1 presentation = hard gate with reason string; advisory "pick one" UI later. |
| Q7 | `ValidatePlay` refactored to an **additive + subtractive restriction pipeline**: mods can add *and remove* play limitations (e.g. red-on-blue legal natively). |
| Q8 | Status effects in phase 1 with **all duration units**: real time, turns, rounds, cards played. |
| Q9 | Response windows open uniformly whenever a window-able situation exists (no per-player ability leak). ENV-tunable duration (5-10s or half turn timer). Default outcome on timeout. **Must-apply auto cards** (totem-of-undying style) trigger before windows. Draw stacking is rebuilt on this mechanism. |
| Q10 | Pseudorandom per match; every roll **logged in the event stream** for future replay. No replay system phase 1. |
| Q11 | **Generic prompt envelope** on the wire. Choice kinds (`choose_color`, `choose_player`, `choose_card`, ...) are data-driven and grow over API versions. |
| Q12 | **Event stream (B)**: server emits explicit ordered event packets for everything that happens; state snapshots remain for reconnect. Client's state-diff animation shim is replaced by a packet source. |
| Q13 | Runtime guards: infinite-loop/chain budgets required. **No balance caps.** Sturdy platform for absurd card counts is a feature. |
| Q14 | **Full semantic validation** at load. Phase 1 is the framework everything later builds on; no cheap-out. |
| Q15 | Phase 1 content store = **server-side JSON folder** (`mods/`). Workshop/buckets later (WIP Decks/Shop UIs are the future home). |
| Q16 | Bots: heuristics now, random-but-valid fallback for modded prompts. Effect-aware bot play is a future direction, not phase 1. |
| Q17 | Stats: count **vanilla matches only**. |
| Q18 | No DB phase 1. Migration runner already exists in repo (memory was stale on this). |
| Q20 | **Full ECS**: entities = cards, players, piles, match. One data model; hooks everywhere trivially. |
| Q21 | Visibility = `(viewer, target, aspect_mask)` pairs incl. piles. Server-authoritative always. Spectators omniscient by default; per-player privacy flag blocks spectator view. Steal-pick cards = visibility grant + `choose_card` prompt. |
| Q22 | Mods define what cards exist and do; decks contain arbitrary multisets of arbitrary cards from active mods. Excluding a card from the deck is how you "disable" it. |
| Q23 | Rule-mods are **plug-and-play**: active mod set = every mod's table laws apply to the whole match automatically. Card hooks ride the card being in the deck. |
| Q24 | **Rewrite in place**, one release (option a). Zero userbase; nothing wrong with changing the system at once. |
| Q25 | Conflict presentation phase 1 = gate + reason string. Advisory UI later. |
| Q26 | Response window timer is **independent of and pauses** the turn timer. |

---

## 5. Architecture overview

```
+---------------------------- server ------------------------------+
|                                                                  |
|  mods/ (JSON) --> ModLoader --> SemanticValidator --> Registry    |
|                    (folder scan) (schema + graph + conflict)     |
|                                                                  |
|  Lobby settings / Deck snapshot --> MatchAssembler                |
|        (mod list + card multiset + settings)                     |
|                     | freezes per-match index registries         |
|                     v                                            |
|  MatchInstance (rewritten)                                       |
|  |- EntityStore       (entities + components, gen handles)       |
|  |- EventBus          (hook catalog, deterministic ordering)     |
|  |- Resolver          (behavior graph walker, budgets)           |
|  |- OpCatalog         (server-curated effect vocabulary)         |
|  |- Timers            (TurnTimer, WindowTimer -- disjoint)       |
|  |- Rng               (per-match seed, logged rolls)             |
|  +- EventSink         (ordered packets, per-recipient view)      |
|                     |                                            |
|                     v                                            |
|  MatchController                                                 |
|  |- BroadcastSnapshot()  (reconnect truth, per-recipient)        |
|  +- EmitEvents()         (event stream, per-recipient view)      |
|                     |                                            |
|                     v  WebSocket (asyncapi.yaml contract)        |
+------------------------------------------------------------------+
+---------------------------- client ------------------------------+
|  ws store --> event listener --> baseBeats (beats) --> GSAP      |
|  |           (replaces watcher      |                            |
|  |            state-diff source)    v                            |
|  |                              stepRenderers (unchanged)        |
|  |- prompt renderer (kind registry + generic fallback)           |
|  |- window timer UI (all-player countdown, pass button)          |
|  +- cardRegistry (defs packet -> kind index -> face hash, atlas) |
+------------------------------------------------------------------+
```

C++ module map (new files under `src/match/` + `include/match/`, old files
deleted per section 17.4):

| Module | File(s) |
| --- | --- |
| Entity/component store | `include/match/ecs/entity_store.hpp`, `src/match/ecs/entity_store.cpp` |
| Component catalog | `include/match/ecs/components.hpp`, registration in `src/match/ecs/component_catalog.cpp` |
| Event bus + hooks | `include/match/ecs/event_bus.hpp`, hook list in `include/match/ecs/hooks.hpp` |
| Op catalog + signatures | `include/match/ops/ops.hpp`, `src/match/ops/*.cpp` (one file per op family) |
| Resolver | `include/match/resolver.hpp`, `src/match/resolver.cpp` |
| Timers | `include/match/timers.hpp`, `src/match/timers.cpp` |
| RNG | `include/match/rng.hpp` |
| Mod loader + validator | `include/match/modload/mod_loader.hpp`, `semantic_validator.hpp`, `src/match/modload/*.cpp` |
| Match assembly | `include/match/match_assembler.hpp`, `src/match/match_assembler.cpp` |
| Event sink + views | `include/match/event_sink.hpp`, `src/match/event_sink.cpp` |
| MatchInstance (rewritten) | `include/match/match_instance.hpp`, `src/match/match_instance.cpp` |
| Vanilla content (data) | `mods/vanilla/mod.json` + `cards.json` + `rules.json` + `decks/classic.json` |

JSON Schemas for all artifact types live in `contract/schemas/`.

---

## 6. Artifacts and file formats

### 6.1 Folder layout

```
mods/
├── vanilla/
│   ├── mod.json            # manifest
│   ├── cards.json          # card definitions
│   ├── rules.json          # rule definitions (incl. status defs)
│   ├── mutations.json      # optional; vanilla ships none
│   └── decks/
│       └── classic.json    # the vanilla deck snapshot
├── space/
│   ├── mod.json
│   ├── cards.json          # black_hole, death_star, ...
│   └── rules.json
└── ...
```

- `UNI_MODS_DIR` env var sets the root (default `mods/`, relative to working
  directory). Scanned at server start.
- One folder per mod. Folder name = suggested id; manifest id is authoritative.
- Subfolders are not nested further (no `mods/space/expansion/` in phase 1 —
  one folder = one mod).

### 6.2 Mod manifest — `mod.json`

```json
{
  "id": "vanilla",
  "name": "UNI Vanilla",
  "version": "1.0.0",
  "api": "1",
  "description": "The standard UNI card set.",
  "author": "eldyn",
  "provides_cards": "cards.json",
  "provides_rules": "rules.json",
  "provides_mutations": "mutations.json"
}
```

- `id`: `[a-z0-9_]+`, 1-32 chars, globally unique across the folder.
- `version`: semver of the *content*, not the schema.
- `api`: schema/contract version this content targets. Loader rejects
  `api` > current engine api version. Engine api version constant starts at
  `1` and lives with the generated contract constants.
- `provides_*` files optional; absent = empty list.

### 6.3 Card definition — `cards.json` entries

```json
{
  "id": "plus4",
  "title": "+4",
  "face": {
    "kind": "text",
    "color": "white",
    "label": "+4",
    "art_version": 1
  },
  "tags": ["draw_penalty", "stackable"],
  "behavior": {
    "on_play": { "nodes": [ "..." ] }
  },
  "window": {
    "when_played": {
      "responders": "@others",
      "respond_with": { "any_tag": ["stackable"] },
      "duration": "env",
      "on_response": "append_to_stack",
      "default": "resolve_stack"
    }
  }
}
```

Field rules:

- `id`: `[a-z0-9_]+`, unique within the mod. Full kind ID = `namespace:id`
  (e.g. `vanilla:plus4`). THE stable string ID used by decks, mutations, and
  behavior references. The manifest `id` is the namespace.
- `face`: declarative face spec. Phase 1 kinds:
  - `text` — color + label (what the composition renderer consumes today)
  - `art_ref` — URL placeholder for the future bucket; shape-validated only
  - `emoji` — single glyph
  - `blank`
  `art_version` participates in the client face-hash (atlas seam,
  `getFaceTexture(cardDefHash)`); bump when art changes.
- `tags`: free-form labels. Tags are the *query vocabulary* used by windows,
  conditions, and mutations ("respond with any card tagged `stackable`") so
  new content interoperates without hardcoding kind IDs.
- `behavior`: named trigger entries keyed by hook name (`"on_play"` =
  `after:play` on this card; `"phase": "before"` allowed). Each carries a
  behavior graph (6.6).
- `window`: declares this card's play opens a response window (section 12).
  Optional. Sugar for a `window` node at the head of the `on_play` graph.

### 6.4 Rule definition — `rules.json` entries

```json
{
  "id": "draw_stacking",
  "title": "Draw Stacking",
  "description": "+2/+4 stack; the next victim may respond with their own.",
  "hooks": [
    { "on": "before:play", "where": { "card_has_tag": "stackable" },
      "nodes": [ "..." ] }
  ]
}
```

- Rule = pure hooks + graphs, no card. Active when its mod is in the match's
  mod list (plug-and-play, Q23).
- `where`: condition filter (10.2) deciding whether the hook fires for a
  given event.
- Status definitions live in the same file, keyed `"statuses": [...]`.

### 6.5 Mutation definition — `mutations.json` entries

```json
{
  "id": "nerf_plus4",
  "target": "vanilla:plus4",
  "mode": "replace",
  "replacement": { "nodes": [ "..." ] }
}
```

Modes and semantics:

| Mode | Semantics |
| --- | --- |
| `replace` | Target's referenced graph is swapped entirely. |
| `wrap` | Injected graph runs before (or after, `"position": "after"`) the original; injected graph may contain a `call_original` op node. |
| `veto` | Adds a deny condition: if the veto's `where` matches, the original graph does not run. Multiple vetoes AND together. |
| `filter` | Transforms the event payload seen by the original graph (e.g. change `n` of a draw). |

Precedence and conflicts:

- Applied in **mod list order** (the lobby's mod list order IS the priority
  order; deterministic, frozen at match start).
- `wrap` + `wrap` on the same target: both run, in mod order.
- `filter` + `filter`: compose in mod order.
- `replace` + (`replace` | `wrap` | `filter`) on the same target: **conflict,
  gate** at load with a reason string naming both mods and the target.
- `veto` coexists with anything (it is a condition, not an alteration).
- A mutation targeting a play-restriction entry another mod removes:
  **conflict, gate**.

### 6.6 Behavior graph format

Flat node list with explicit routing. Same shape philosophy as the client
animation IR (flat list + position offsets) — deliberate, the pattern is
proven, and future Lua compiles down to it.

```json
{
  "nodes": [
    { "id": "n1", "op": "prompt",
      "args": { "kind": "choose_color", "target": "@self" }, "next": "n2" },
    { "id": "n2", "op": "set_active_type",
      "args": { "from_prompt": "n1" }, "next": "n3" },
    { "id": "n3", "op": "draw_cards",
      "args": { "target": "@next_player", "n": 4 }, "next": "n4" },
    { "id": "n4", "op": "advance_turn", "args": {} }
  ]
}
```

Node types:

| Node | Meaning |
| --- | --- |
| `op` node | Runs one op from the catalog. `args` per op signature (10). |
| `window` node | Opens a response window (12). `default` routes to a node id; responses route via `on_response` mapping (responder filter digest to node id). Resolution continues down the chosen route. |
| `branch` node | `"cases": [{"when": <condition>, "next": id}, ...]`, `"else": id`. Conditions per 10.2. |
| `fork` node | `"branches": [ids]` — subgraphs run in declared order, sequentially, in phase 1 (parallel semantics are animation-side only; engine resolution stays serial and deterministic; fork exists for readability and future parallelism). |
| `schedule` node | Defers the `next` subgraph until a duration spec elapses (WILD Patriota pattern). Implemented as a scheduled status (11.4). |

Context selector DSL (used in op args wherever an entity is addressed):

| Selector | Resolves to |
| --- | --- |
| `@self` | Player who played the card / triggered the rule. |
| `@target` | Current resolution target (set by targeting ops/windows). |
| `@responder` | Player responding inside a window route. |
| `@current_player` | Whoever owns the turn right now. |
| `@next_player` / `@prev_player` | Seat neighbors in current direction. |
| `@all_players` / `@others` | Sets; ops applied to sets iterate in seat order. |
| `@choose_player` | Opens a `choose_player` prompt implicitly and binds the result (sugar; expands to prompt + bind). |
| `@draw_pile` / `@discard_pile` / `@match` | Non-player entities. |
| `@card` | The card entity being resolved (card behavior context only). |

### 6.7 Deck (LobbySettings snapshot) — `decks/*.json`

```json
{
  "id": "classic",
  "name": "Classic",
  "namespace": "vanilla",
  "mods": ["vanilla"],
  "cards": {
    "vanilla:red_0": 2,
    "vanilla:plus4": 4,
    "space:black_hole": 1
  },
  "settings": {
    "match": { "score_limit": 500 },
    "chaos": { "blackhole_threshold": 99 }
  }
}
```

- `cards` is a multiset: kind ID to count. Arbitrary kinds, arbitrary counts;
  all kinds must resolve in the mod list (validation).
- `settings` is a typed bag: engine + mods declare named settings with
  defaults in their manifest (`"settings": [{"id", "type", "default",
  "range"}]`); deck values are validated against those declarations. Unknown
  setting keys = validation error (typos must not pass silently).
- A freestyle lobby = same structure, not persisted. Lobby UI edits it live.
- Selecting a deck = loading the whole snapshot (mods + cards + settings) at
  once (Q22/Q23).

### 6.8 Wire identity — `CompactCardV2`

```cpp
// include/common/match/card_types.hpp (rewritten)
// bits 31..24 : mod_index   (position in frozen match mod list)
// bits 23..12 : kind_index  (position in that mod's card kind list)
// bits 11..0  : instance_id (copy number of this kind within the deck)
struct CompactCardV2 {
	uint32_t bits;
};
```

- 256 mods max, 4096 kinds per mod, 4096 copies per kind per deck. Bounds
  validated at assembly; exceeding any = load error with counts.
- Instance IDs assigned deterministically at assembly (kind-sorted, instance
  counter per kind) so serialization is stable.
- Indices are meaningless outside one match (registry frozen at start).
  Anything that outlives a match stores kind string IDs + counts and
  re-indexes on restore.
- Existing WS payload style stays: cards serialize as plain integers.

---

## 7. ECS core

### 7.1 EntityStore

- Dense `std::vector` per component type; entity = `{index: uint32,
  generation: uint32}` handle. Dead-handle access = validated error path
  (returns null / `std::nullopt`), never UB.
- Entities are untyped; meaning comes from components. Expected archetype
  sets (validation-tested, not enforced): card = `card_identity` + `in_zone`
  + `face_spec` + optional `card_behavior` / `auto_trigger` / `window_spec`;
  player = `player_info` + `hand` + statuses; pile = `pile_contents`; match
  = `match_meta`.

### 7.2 Component catalog (phase 1)

All components are data-only structs, registered in one catalog with string
IDs. Mods reference components by these IDs in op args; the catalog is the
extension point for future component kinds.

| ID | On | Shape (essentials) |
| --- | --- | --- |
| `card_identity` | card | kind index + frozen kind ref |
| `in_zone` | card | zone ref (hand of player X / draw / discard / limbo) + ordinal |
| `face_spec` | card | copy of def face (hash basis) |
| `card_behavior` | card | trigger to graph map (copied from def at assembly, post-mutation) |
| `auto_trigger` | card | condition + graph + `must_apply` flag (12.5) |
| `window_spec` | card | window declaration (copied from def) |
| `player_info` | player | username, seat, connected, is_bot, ready |
| `hand` | player | ordered list of card entity refs (display order) |
| `turn_state` | player | is_current, turn_deadline, extra_turns_pending |
| `status` | any | status instance: kind, magnitude, stack_policy, duration (11) |
| `visibility_grant` | any | (viewer, aspect_mask, expires) entries |
| `draw_debt` | player | accumulated pending draw count (draw-stacking successor) |
| `play_restriction` | match | restriction pipeline entries (13.3) |
| `active_type_req` | match | current required card type (or none) |
| `window_state` | match | open window: responders, deadline, default route, collected responses |
| `pending_schedule` | match | deferred graphs with duration specs |
| `match_meta` | match | mod list, direction, round counter, rng seed, budgets ledger |
| `placements` | match | finish order |
| `rng_state` | match | seed + op counter |

### 7.3 Systems (mods) at runtime

- A mod's system = (a) hooks subscribed from its rule defs + card behaviors +
  mutations, (b) execution = walking behavior graphs via the Resolver.
- No arbitrary native code path exists for content in phase 1. The only
  "code" is the op catalog implementations. This IS the sandbox: ops are
  total, bounded functions over the entity store.
- Hook dispatch order: mod list order, then within a mod, registration order.
  Before-hooks may mutate the event payload; a before-hook returning `veto`
  cancels the engine's default handling for that event. All before-hooks
  still run (order deterministic); veto flag checked after.

### 7.4 MatchInstance responsibilities (rewritten)

Owns: EntityStore, EventBus, Resolver, Timers, RNG, EventSink, frozen
registries (mod list, kind table, index maps). Public surface for controllers
stays close to today's: `PlayCard`, `DrawCard`, `SubmitInput`, `Tick`. Internal
flow:

```
input --> restriction pipeline (play_restriction entries)
      --> before:play hooks --> engine default (move card, after:play)
      --> Resolver drains graph queue (ops emit their own events)
      --> advance turn when queue empty and no window open
Tick  --> timers (turn, window, real-time statuses)
      --> scheduled graphs whose duration elapsed
      --> bot turns / AFK takeovers
```

---

## 8. Hook catalog (phase 1)

Every hook has `before:` and `after:` variants unless noted. Veto column =
before-variant may veto the engine default.

| Hook | Fires when | Payload essentials | Veto |
| --- | --- | --- | --- |
| `match_start` / `match_end` | assembly done / finished | settings snapshot | no |
| `round_start` / `round_end` | full seat cycle completed | round number | no |
| `turn_start` / `turn_end` | player gains/loses turn | player | yes (turn_end veto = extra turn) |
| `play` | card play attempted/occurred | card, player, from zone | yes |
| `draw_attempt` | about to draw (per card) | player, source pile | yes (skip this draw) |
| `draw` | card drawn | card, player | yes (undo) |
| `shuffle` | discard reshuffled into draw | pile sizes | no |
| `pile_empty` | draw pile exhausted (post-reshuffle check) | pile | yes (block reshuffle, e.g. stalemate rule) |
| `hand_empty` | player shed last card | player | yes (block win) |
| `win_check` | after any state settle | candidate player or none | yes (block / declare other) |
| `status_applied` / `status_removed` | status change | target, status instance | no |
| `window_open` / `window_close` | section 12 | window state | no |
| `effect_applied` | op node resolved | op, args summary, targets | no |
| `card_entered_zone` / `card_left_zone` | zone membership change | card, zones | no |
| `roll` | RNG op consumed | spec, outcome | no |
| `visibility_granted` / `visibility_revoked` | 14.4 | viewer, target, aspects | no |

- Conditions (`where` filters) may inspect **true engine state** — engine-side
  conditions are omniscient. Visibility filtering applies ONLY to what leaves
  the server in packets. This distinction is load-bearing (Spy and Flag both
  need true-state reads; only packet payloads are filtered).
- Catalog is append-only in future versions. New hooks = api version bump;
  the `api` gate in the manifest rejects unknown-major content.

*(Numbering note: section 9 was folded into sections 8 and 10 — hooks and
ops — during drafting. All cross-references are stable as numbered.)*

---

## 10. Effect op vocabulary (phase 1 catalog)

Ops are total, bounded functions: `(store, args, ctx) -> effects + events`.
Signatures below are normative; the C++ op table in `ops.hpp` mirrors them.

### 10.1 Card and pile ops

| Op | Args | Effect |
| --- | --- | --- |
| `draw_cards` | `target`, `n` (0..1000), `from` (pile, default draw) | Moves n cards; emits `cards_drawn`; fires `draw_attempt`/`draw` per card. Auto-reshuffles (emits `reshuffle`) when draw empties, unless reshuffle vetoed. |
| `move_card` | `card`, `to_zone` | Single card relocation (steal, return). |
| `transfer_card` | `from_player`, `to_player`, `selector` (`random` / `chosen` / `tag:x` / `kind:x`) | Steal pattern. `chosen` expands to a `choose_card` prompt on a revealed subset (visibility grant during the prompt). |
| `pass_hands` | `direction` (`forward`/`backward`) | All hands rotate. |
| `swap_hands` | `a`, `b` | Two players exchange hand contents. |
| `redistribute_hands` | `mode` (`even`) | Comunismo: pool all cards, deal evenly, remainder per seat order, deterministic. |
| `materialize_card` | `target_player`, `kind`, `zone` (default hand) | Creates a NEW instance of a kind beyond the deck multiset (Black Hole pattern). Budgeted like any op. |
| `remove_card` / `replace_card` | card / card + kind | Destruction / transmutation (limbo zone; removed cards never return). |
| `peek_pile` | `viewer`, `pile`, `n` | Grants temporary visibility on top-n (Spy pattern). Emits `visibility_granted`. |

### 10.2 Condition ops (usable in `branch`, `where`, `veto`)

`has_card_kind(target, kind)`, `has_card_tag(target, tag)`, `hand_size(target,
cmp, n)`, `active_type_is(type)`, `top_of_discard(kind|tag|type)`,
`status_active(target, status_kind)`, `draw_debt(target, cmp, n)`,
`rolled(cmp, n)` (most recent `roll` in ctx), `is_direction(fwd|rev)`,
`round(cmp, n)`, `turns_elapsed(cmp, n)`, `always`, `never`.

### 10.3 Turn and flow ops

`advance_turn`, `skip_turn(target)`, `reverse_direction`, `extra_turn(target)`,
`redirect_turn(target)` (jump-in resolution), `set_turn_timer(target,
duration)`.

### 10.4 Status ops

`apply_status(target, status_kind, params, duration, stack_policy)`,
`remove_status(target, status_kind|instance)`, `modify_status(target, kind,
delta)`. Status kinds are mod-declared data (11.1); `apply_status` references
a status def ID like `vanilla:draw_debt` or `space:shielded`.

### 10.5 Prompt ops

`prompt(kind, target, payload, timeout, default)` — opens the generic prompt
envelope (14.3). Result binds into ctx for `from_prompt` args. The
`@choose_player`-style selectors desugar to this.

### 10.6 Randomness

`roll(spec)` — `spec = {sides, count, keep?}`; result list in ctx; emits
`roll_result` with seed/counter/outcomes (Q10 logging).

### 10.7 Visibility ops

`grant_visibility(viewer, target, aspects, duration)`, `revoke_visibility`.
Aspects: `count`, `color`, `value`, `identity`, `position`. Targets: any
entity (a player's hand, the draw pile, a card).

### 10.8 Type and restriction ops

`set_active_type(type|from_prompt)`, `clear_active_type`,
`add_restriction(entry_def)`, `remove_restriction(entry_id)` — the Q7
pipeline. Restriction entries (data): `{id, phase: allow|deny, condition}`.
Engine default play legality = vanilla's entries are simply the first entries
in the pipeline, removable like any other.

### 10.9 Win ops

`check_win` (fires `win_check` hooks; engine default = hand_empty player
wins), `declare_winner(player, kind: normal|special)` (Black Hole = special),
`add_placement(player)` (placements for shed-order lobbies).

### 10.10 Scheduling and meta

`schedule(duration, subgraph)` (node-level, 6.6), `emit_signal(name, payload)`
(custom named packet on the event stream — the Lua/plugin seam and the VFX
hint carrier), `call_original` (inside `wrap` mutations only).

### 10.11 Sufficiency cross-check — every designed card must be expressible.
The vanilla + sample-mod content in WP8 must prove each row in an integration
test (this table is the expressibility proof obligation):

| Card / rule | Expression |
| --- | --- |
| Shield | `auto_trigger` on draw_debt application: play self, cancel (veto route) or redirect stack |
| Cleanse | window responder with `respond_with` kind filter; response route = `remove_status` draw_debt |
| Totem (must-apply) | `auto_trigger` + `must_apply: true` (12.5) |
| Black Hole | `after:draw` branch `hand_size >= threshold(settings)` then `materialize_card` + deny-veto restrictions on others' responses + `declare_winner special` on play |
| Death Star (+10) | `draw_cards n:10` + window on `@others` |
| Flag | `branch` on `has_card_kind/has_card_tag` of `@next_player` then `draw_cards n:2` |
| WILD Flag | same with `@all_players` |
| Spy | `peek_pile` / `grant_visibility identity` + duration |
| Comunismo | `redistribute_hands even` (reaction variant = window responder on others' plays) |
| WILD Patriota | `schedule` until next turn start |
| Dice card | `roll` + `branch` on `rolled` |
| jump_in (rule) | `before:play` + `add_restriction` allow-if-identical-out-of-turn |
| seven_zero (rule) | `where value=7` then `swap_hands @choose_player`; `value=0` then `pass_hands` |
| progressive / force_play (rules) | `after:draw` conditions driving keep-drawing / force-play prompt |
| red-on-blue native (Q7 demo) | `remove_restriction vanilla:match_type_or_value` + `add_restriction` color-pair allow entry |
| Unlucky/Lucky Color | `branch` on hand contents + `draw_cards` with color filter (draw op gains optional `filter: {color}` in phase 1 — noted for WP5) |

---

## 11. Status effects

### 11.1 Status definition (mod data, in `rules.json` `"statuses"` array)

```json
{
  "id": "shielded",
  "title": "Shielded",
  "stack_policy": "replace",
  "hidden": false
}
```

Stack policies: `replace` (re-apply resets duration), `accumulate` (magnitudes
add), `independent` (separate instances, own durations), `cap:N` (accumulate
up to N).

### 11.2 Duration units (Q8 — ALL required)

`{ "unit": "ms" | "turns" | "rounds" | "cards_played", "value": n }`.
Compound durations allowed: `turns: 2 AND ms: 30000` = whichever elapses
first. Ticks: `ms` statuses check in `Tick` via wall clock; `turns`
decrement on `turn_end` of the *owner's* turn; `rounds` on `round_end`;
`cards_played` on any `after:play`. Expiry fires `status_removed` + the
status's optional `on_expire` graph.

### 11.3 Private statuses

`hidden: true` statuses are not emitted to other players (owner only +
spectators unless that player's privacy flag is set). Public statuses emit
to all.

### 11.4 Scheduled graphs

`schedule` nodes create a `pending_schedule` entry with the duration spec;
elapsed entries execute their subgraph in `Tick`. This is also how real-time
"after 5 minutes" penalty rules work (survey idea: +3 cards if holding >5
after 5 minutes = scheduled check + branch).

---

## 12. Response windows and timers

### 12.1 Model

- A window-able situation = any behavior graph reaching a `window` node. A
  card's `window.when_played` declaration is sugar for a `window` node at the
  head of its `on_play` graph (one mechanism, no second path).
- **Uniform windows (Q21.3):** when the situation exists, ALL players get
  the window UI + timer, regardless of playability. No information leaks —
  no attribution, no per-player signaling.
- Window carries: responder set (usually `@others` or `@all_players`),
  eligible-response filter (`respond_with`: tag/kind/condition), deadline,
  default route, response routes.

### 12.2 Timing

- Duration = `UNI_WINDOW_MS` when `UNI_WINDOW_MODE=fixed` (defaults: 7000ms,
  fixed); `min(UNI_WINDOW_MS, half of remaining turn time)` when
  `UNI_WINDOW_MODE=half_turn`.
- **Turn clock pauses** while a window is open (Q26). WindowTimer and
  TurnTimer are disjoint timer classes; only one runs at a time. Opening a
  window suspends TurnTimer; closing resumes it with remaining time.
- Early close: when every player in the responder set has sent `pass` (or
  acted), the window closes immediately. Client shows a Pass button; pass is
  the default action on timeout.
- Timeout = default route executes. Bots pass unless their policy fires
  (16).

### 12.3 Responses

- A response = playing an eligible card (moves through the normal play
  pipeline: restrictions checked, `before:play` fires) or a declared
  non-card action (phase 1: cards only; non-card responses are a future
  extension point).
- Multiple responders in one window: responses collected until deadline or
  all-pass. **First collected response wins** (server arrival order;
  deterministic tie-break by seat for same-tick arrivals). Losers' cards
  return to hand unplayed. `window_close` event lists winner + action.

### 12.4 Draw stacking rebuild (replaces current `pending_draws` logic)

- `+N` play: window (responders `@others` + `@next_player`, respond with
  `stackable` tag). Each response appends its own N to a `draw_debt` status
  on the then-target and RE-OPENS the window (window chaining legal; the
  chain shares one budget ledger). Default route: `draw_cards` equal to the
  debt on the current target, debt cleared.
- Existing `draw_stacking.cpp` behavior is fully subsumed; old file deleted
  at swap.

### 12.5 Must-apply auto cards (totem of undying)

- `auto_trigger` with `must_apply: true` + condition fires **before** the
  window would open, automatically playing itself (no player agency); the
  situation then re-evaluates (window may no longer exist). Emits
  `auto_played` event.
- One per triggering event. Guards prevent totem loops: a must-apply card
  cannot trigger itself; re-trigger depth capped at 4 (13.2).

---

## 13. Validation and resilience

### 13.1 Load-time validation (full semantic, Q14)

Runs per mod folder scan AND per match assembly (a mod subset may conflict
only in combination). Ordered checks; first failure = reject mod/deck with
machine-readable error `{check, artifact, path, message}` surfaced to the
lobby as the gate reason string (Q25):

1. JSON Schema conformance (schemas in `contract/schemas/`).
2. ID syntax + uniqueness (mod ids, kind ids, rule ids, status ids,
   restriction entry ids, mutation ids within namespace).
3. Reference resolution: every kind ref in decks/graphs/mutations exists in
   the active mod set; every status/tag/restriction ref resolves; every
   graph `next`/route target exists (no dangling edges).
4. Op arity/type check: args match op signatures (types: selector, int
   bounds, duration spec, aspect mask, condition object, kind ref).
5. Graph structure: cycle detection (error — graphs must be DAGs; loops are
   expressed via window re-open or schedule, never cyclic edges);
   unreachable nodes (error — dead code means the author erred); `window`
   nodes must declare a default route; `call_original` only inside
   wrap-mutation graphs.
6. Mutation conflict detection per the 6.5 table.
7. Deck validation: all kinds resolve; counts within index-space bounds
   (6.8); settings validate against declarations (type, range, unknown keys
   rejected).
8. Selector sanity: `@responder` only inside window routes; `@card` only in
   card behavior context.

### 13.2 Runtime guards (Q13) — all ENV-tunable

| Guard | Default | On breach |
| --- | --- | --- |
| Chain step budget (ops executed per single graph resolution, incl. window chains) | `UNI_CHAIN_BUDGET=10000` | Abort current chain: skip remaining nodes, emit `chain_aborted {mod, node}`, log ERROR, match continues |
| Hook re-entry depth (same hook firing nested from its own graph) | `UNI_REENTRY_CAP=8` | Same abort path |
| Event budget per match | `UNI_EVENT_BUDGET=100000` | Drop `emit_signal` packets only (state-changing events are never dropped; signal spam is the only droppable class), log |
| Must-apply re-trigger depth | 4 | Treat as non-must-apply, log WARN |
| Mod disarm threshold: chain aborts attributable to one mod | `UNI_MOD_DISARM_THRESHOLD=3` | Mod's systems neutralized for the match, `mod_disarmed` packet, log ERROR |

Guards protect non-termination and resource exhaustion ONLY. A deck with
500 distinct kinds and 1000-card hands is valid and must run.

### 13.3 Restriction pipeline (`play_restriction`, Q7)

- Entries evaluated in order: `deny` entries first-match-wins veto the play
  attempt with the entry's id as reason; `allow` entries can rescue an
  otherwise-denied play (jump-in pattern: deny out-of-turn, allow
  identical-card). Pipeline denial emits `play_rejected {reason_id}`.
- Vanilla ships its entries as data — `vanilla:turn_order`,
  `vanilla:match_type_or_value`, `vanilla:must_own_card` — removable via
  `remove_restriction`.
- Adding/removing entries mid-match is legal (that is the point); entries
  are components on the match entity.

---

## 14. Wire contract

### 14.1 Event stream (Q12-B)

New `ServerAction` family in `contract/asyncapi.yaml`: a `match.event`
message with generic envelope:

```json
{ "seq": 42, "type": "cards_drawn", "payload": { } }
```

- `seq`: per-match monotonic uint32. Filtering may drop a packet for one
  recipient but never renumbers (gap-free per recipient stream).
- Ordering: server emits in resolution order; WS guarantees per-connection
  order. Client on `seq` gap > 1: flush animation queue, mark desynced, next
  snapshot reconciles (snapshots still broadcast at every settle point as
  today).
- Per-recipient filtering (server-side view building, 14.5): same event
  type, different payload per viewer.
- `asyncapi.yaml` remains the single source of truth; both codegens
  (`generate_contract_hpp.py`, `generate_ws_hpp.py`, frontend
  `generate-schemas.js`) extended with the envelope + payload schemas.
  Payload schemas versioned by name (`x-packet-v1`). **Unknown `type`
  strings MUST be ignored by clients** (forward-compat rule, normative).

### 14.2 Packet catalog (phase 1; per-recipient filtering rules normative)

| type | payload essentials | visibility rule |
| --- | --- | --- |
| `match_start` | defs digest, mod list, deck id/name, settings | all |
| `defs` | full kind table: per mod {id, version, index}, per kind {index, string_id, face, tags} | all (needed for face hashing) |
| `card_played` | player, card identity, from zone ordinal | all (played cards are public by nature) |
| `play_rejected` | player, reason_id | target player only |
| `cards_drawn` | player, count, source pile | owner gets card identities; others get count only |
| `reshuffle` | new draw size, new discard size | all |
| `status_applied` / `status_removed` | target, status kind, magnitude, duration unit (not remaining ms), instance id | public statuses all; `hidden` statuses owner-only |
| `window_open` | deadline (ms remaining), responder set, eligible filter digest, window id | all players get timer; filter digest identical for all (uniform windows — no leak) |
| `window_response` | player, action (card identity / pass) | all (public action) |
| `window_close` | outcome: default / winner + action | all |
| `auto_played` | player, card identity, trigger summary | all |
| `prompt_open` | prompt_id, kind, payload, response_schema, deadline | target player only |
| `prompt_close` | prompt_id, outcome (answered / timed out / cancelled) | target player only |
| `turn_advance` | from, to, direction, deadline | all |
| `round_advance` | round number | all |
| `roll_result` | spec, outcomes, roll counter | all |
| `signal` | name, payload (mod-defined; animation/VFX hint carrier) | per signal declaration in mod manifest (`"signals": [{"name", "audience"}]`) |
| `chain_aborted` | mod, node, reason | all (dev-mode toast) |
| `mod_disarmed` | mod id, reason | all |
| `visibility_granted` / `visibility_revoked` | target, aspects | viewer only |
| `placement` | player, place | all |
| `match_end` | winner, placements, final digest | all |

Prompt responses travel on the existing input channel, extended:
`{prompt_id, value}` where `value` is JSON validated server-side against the
prompt's response schema. The three current `Action` kinds become prompt
kinds; the old enum is deleted.

### 14.3 Prompt envelope (Q11)

```json
{
  "kind": "choose_color",
  "payload": { "options": ["red", "blue", "green", "yellow"] },
  "response_schema": { "type": "string", "enum": ["red", "blue", "green", "yellow"] },
  "timeout_ms": 15000,
  "default": "red"
}
```

- Phase 1 kinds shipped by engine + vanilla: `choose_color`, `choose_player`,
  `choose_card`, `choose_yes_no`, `choose_value`. Kind registry is open: mods
  declare new kinds in the manifest (`"prompts": [{"kind", "payload_schema",
  "response_schema"}]`); validator enforces schemas; clients render unknown
  kinds with the generic fallback UI (render from `response_schema`: enum =
  buttons, integer range = stepper, boolean = toggle).
- Prompt timeout: `default` value binds if present, else the graph's
  `branch.else`.

### 14.4 Visibility model (Q21)

- Server maintains `visibility_grant` pairs; the packet view-builder is the
  ONLY consumer. Engine conditions read true state (8, load-bearing note).
- Spectator connections: default omniscient. Per-player
  `privacy_from_spectators: true` (phase 1: lobby-level per-player toggle in
  lobby settings) strips that player's hidden aspects from spectator views.
- Base visibility without grants: own hand full identity; others' hands
  count only; draw pile count only; discard top identity + count; all
  played/public events.

### 14.5 Snapshot (unchanged role)

`BroadcastSnapshot` stays the reconnect truth, built through the same view
builder (per-recipient), now including window state, statuses, open prompts,
and the seq watermark. Client on reconnect: take snapshot, flush animation
queue, resume.

---

## 15. RNG

- Per-match seed: random 64-bit at assembly (or explicit seed via settings —
  dev tool for tests). splitmix64 stream, monotonic op counter.
- Every `roll` op: counter++, outcome computed, `roll_result` emitted with
  seed id + counter + outcomes. Deterministic replay = seed + ordered op log
  (future; not built phase 1).

---

## 16. Bots (phase 1)

- Card play: existing heuristics operate on server-provided playability
  flags (own-hand payload already carries playable flags — keep that
  property in the new snapshot builder).
- Prompts: known kinds = heuristic answer (choose_color: most common color
  in hand; choose_player: fewest cards; choose_card: first valid; yes_no:
  heuristic; choose_value: middle). Unknown kinds = `default` if present,
  else first schema-valid value.
- Windows: pass by default; 20% chance to respond with the best eligible
  card (deterministic per bot seed so tests are stable).
- Must-apply cards: no bot agency involved — auto triggers just fire.
- Bot policy is a small strategy interface (`IBotPolicy`) so the future
  effect-aware "learning" direction plugs in without touching the engine.

---

## 17. Migration (rewrite in place, Q24-a)

### 17.1 Order

Old engine keeps running on main until the WP13 swap commit. New engine
files land alongside; vanilla content authored as data; the swap is one
atomic commit changing `Lobby`'s match ownership + deleting old paths.
"Rewrite in place" refers to the END STATE — no v2 namespace, no compat
shims, no parallel protocols — not to big-banging the repo in one
unreviewable commit.

### 17.2 Content authoring

- `mods/vanilla/` authored by hand from the current engine's semantics. The
  current `standard.cpp`, the effects, and the 6 rule mods are the reference
  behavior — read them, transcribe them to data, do not invent.
- Golden transcripts: BEFORE deleting any old code, run scripted matches on
  the CURRENT engine and capture ordered event/state transcripts as test
  fixtures. Integration tests then assert the data-driven engine reproduces
  those outcomes.

### 17.3 Controller changes

- `MatchController`: broadcast path becomes view-builder + event sink; input
  path becomes restriction pipeline + resolver; the existing
  broadcast-between-steps callback points become event emission points.
- `LobbyController`: lobby settings gains the deck snapshot structure; deck
  list endpoint `GET /api/decks` (id, name, namespace, mod requirements —
  from the mods folder scan).

### 17.4 Deletion checklist (post-swap commit)

Backend: `include/common/match/{card_types.hpp, effect.hpp, matchrule.hpp}`
(old versions), `include/match/{effect_registry.hpp, rule_registry.hpp,
match_state.hpp}`, `include/match/effects/*`, `include/match/rules/*`,
`src/match/{effects,rules}/*`, the old `Action` prompt trio, `pending_draws`
logic, `SerializeHandFor` (subsumed by the view builder).
Frontend: `baseBeats` watcher diff source, old `Action`-based prompt
handling.

### 17.5 Frontend changes (bounded; only what Q12/Q9 require)

1. `baseBeats.svelte.ts`: watcher (state-diff) replaced by an event-packet
   source feeding the same beat vocabulary. Mapping (normative):
   `card_played` = play beat; `cards_drawn` = draw beat (count/identity
   aware); `reshuffle` = reshuffle beat; `window_open`/`window_close` = new
   timer beats; `auto_played` = play-beat variant; `turn_advance` = turn
   indicator tween; `status_applied` = phase 1 toast only (VFX hook point).
   Renderer stack (stepRenderers, GSAP queue, cardRegistry) unchanged.
2. Window timer UI: all-player countdown chip + Pass button; early close on
   all-pass.
3. Generic prompt renderer: kind registry (`choose_color` reuses the
   existing color picker; `choose_player` reuses the existing target picker;
   `choose_card` = new simple card picker; fallback renderer built from
   `response_schema`).
4. Deck picker in lobby: dropdown fed by `GET /api/decks`, loads the
   snapshot into lobby settings; freestyle editing stays as-is.
5. `seq` watermark + desync/flush logic in the game store.

Everything else (Decks/Shop WIP screens, workshop) untouched.

---

## 18. Database — none

No schema changes, no new tables, no queries. Mods and decks are files.
Stats gating (19) is write-side only.

---

## 19. Stats gating (Q17)

- `player_stats` updates only when the match mod set is exactly
  `["vanilla"]` AND the deck kind multiset equals the vanilla classic deck
  multiset. Comparator = sorted multiset equality on kind string IDs.
- Match rows / placements still recorded for all matches (match history
  stays complete).

---

## 20. Testing strategy

- **Unit:** EntityStore (generational handles, dead-access safety); op
  catalog (each op: happy path + bounds + veto paths); validator (each
  check with positive and negative fixtures); timers (pause/resume,
  window/turn disjointness); RNG determinism; view builder (per-recipient
  payload shapes).
- **Integration (golden matches):** scripted input sequences; full ordered
  event stream + final state asserted against the hand-authored transcripts
  (17.2). Coverage: every op at least once; every pattern in 10.11; window
  chaining (draw stacking war); totem auto-play; Black Hole flow; 4-bot
  match (stability + budget); reconnect mid-window.
- **Adversarial:** deliberately authored loop decks (A triggers B triggers A
  via schedule) must hit budgets, abort cleanly, match survives; disarm
  escalation test; 1000-card hand match completes.
- **Validation fuzz:** random JSON decks/mods through the loader. Only
  requirement: loader never crashes; always returns structured error or
  success. Nightly CI job.
- **ASan/UBSan** on for the test preset; parity with existing hardened CI.
- **Frontend:** baseBeats event-source tests (packet fixtures replace diff
  fixtures); prompt renderer fallback test; window timer early-close test.

---

## 21. Work packages (implementation order; each independently verifiable;
suitable for separate agent sessions)

| WP | Content | Acceptance |
| --- | --- | --- |
| WP1 | Contract: asyncapi event family, envelope + packet schemas, prompt envelope, defs packet; run all three codegens | Generated headers/TS compile; unknown-type ignore rule documented in contract |
| WP2 | `contract/schemas/` JSON Schemas + `ModLoader` folder scan + manifest/api gating | Malformed folder = structured errors; `api` mismatch rejected |
| WP3 | ECS core: EntityStore, component catalog, generational handles + unit tests | Unit suite green incl. dead-handle safety |
| WP4 | EventBus + hook catalog + Resolver + op catalog skeleton (signatures, stubs) + budget ledger | Hook-order determinism test; budget abort path test with stub op |
| WP5 | Op implementations (all 10.x families) + condition ops + unit tests | Every op unit-tested incl. bounds |
| WP6 | Status subsystem + Timers (Turn/Window disjoint) + RNG + scheduling | Pause/resume tests; duration unit tests; roll logging |
| WP7 | Semantic validator (all 13.1 checks) + mutation conflict gate + restriction pipeline | Positive + negative fixtures per check; gate message format |
| WP8 | Vanilla content authoring (manifest, cards, rules incl. 6 mods as data, classic deck) + golden transcripts captured from CURRENT engine before any deletion | Golden transcripts committed as fixtures; data passes validator |
| WP9 | MatchInstance rewrite: assembly, input flow, resolution, win/placement flow | Plays scripted matches headless (no WS yet) |
| WP10 | Event sink + view builder + per-recipient filtering + snapshots + visibility | View-builder unit tests per packet visibility rule |
| WP11 | MatchController + LobbyController + deck endpoint wiring + prompt input channel | Full WS match playable vs dev client |
| WP12 | Bots on new flow (`IBotPolicy` + heuristics + prompt fallbacks) + stats gating | 4-bot match completes; stats gate unit test |
| WP13 | SWAP: lobby owns new MatchInstance; delete old engine paths (17.4); CHANGELOG + changelog.html + VERSION bump | Repo builds without old files; golden integration suite green |
| WP14 | Frontend: event source swap in baseBeats + seq/desync + prompt renderer + window timer UI + deck picker | Frontend tests green; manual match vs dev server |
| WP15 | Adversarial + fuzz suites; nightly CI job; docs (README engine section, this spec linked) | Suites in CI; fuzz job green for 10k random decks |

Dependencies: WP1 before WP2; WP2 before WP7; WP3+WP7 before WP4; WP4 before
WP5; WP5+WP6 before WP9; WP9 before WP10; WP10 before WP11; WP11 before
WP13. WP8 needs WP7 for validation but MUST START before WP13 (transcripts
come from the old engine). WP12 after WP11. WP14 after WP13 (needs the new
server to test against). WP15 last.

---

## 22. Work habits for implementing agents (binding)

### 22.1 Communication and context discipline

- **Caveman ultra** reply style for conversational output. Technical terms,
  code, error strings: verbatim. Code, commits, PRs, docs: normal register.
- **Context is precious.** Route verbose throwaway work out of the main
  conversation; use subagents for multi-file exploration floods; report
  summaries, not dumps.
- Use **context-mode** (`ctx_batch_execute`, `ctx_search`, `ctx_execute_file`)
  to keep large outputs out of context. After any resume/compaction,
  `ctx_search(sort: "timeline")` before asking what was happening.
- Use **Headroom** compression on oversized tool outputs before reasoning
  over them.
- Use **codebase-memory-mcp** FIRST for structural code discovery
  (`search_graph`, `trace_path`, `get_code_snippet`, `get_architecture`);
  grep/glob only for non-code text or targeted post-CBM checks. Check
  `index_status` / `check_index_coverage` before trusting coverage.
- Use **Serena** for symbolic navigation and edits (`get_symbols_overview`,
  `find_symbol`, `find_referencing_symbols`, symbolic replace) over line
  surgery. Check Serena memories at task start; write durable discoveries
  back as memories.
- Use **context7** when touching any library API that may have drifted.
- **rtk** prefix on shell commands where filtering helps (git, tests,
  builds).

### 22.2 Repo conventions (from project memories — binding)

- Commits: atomic, subject-only, no body, no co-author trailer.
- Pre-commit: `npm run format` + lint; style is spaces, not tabs.
- Comments: `INFO/BUG/FIXME/TODO/ERROR/WARN` prefixes, 80-col, end-aligned;
  sparing — code is self-descriptive.
- Every meaningful commit updates BOTH `CHANGELOG.md` and `changelog.html`.
- VERSION file is the version source of truth; releases = tag `vX.Y.Z`.
- Never fast-forward `main` to a feature-branch tip; cherry-pick release
  commits only (0.4.7 incident).
- TDD per superpowers: red-green-refactor. Verification before completion:
  run the suite, show the command and its result, then claim done.
- Never commit secrets. Never commit or push unless the task explicitly
  says so.

### 22.3 Per-WP protocol

1. Read this spec's section for your WP + the decision record (4) —
   decisions are closed. If something seems wrong, surface it; do not
   silently diverge.
2. Verify current code state for every file you touch. This spec's file
   paths are point-in-time 2026-09-19; the repo moves.
3. Write tests first where the WP defines behavior (op units, validator
   checks, golden transcripts).
4. Keep diffs scoped to the WP. Unrelated cleanup = separate task.
5. CHANGELOG entries for user-visible changes land at swap time (WP13), not
   before.
6. On WP completion: report acceptance evidence (command + result), not
   assertions.

---

## 23. Open items deliberately deferred (future phases, rough order)

1. Workshop UI + deck sharing + mod distribution (Decks/Shop WIP screens
   are the landing zone).
2. Asset buckets (OCI) + CDN + `art_ref` face kind activation + IndexedDB
   atlas persistence (`getFaceTexture(hash)` seam is ready).
3. Lua scripting layer compiling to behavior graphs (ops remain the sandbox
   boundary; `emit_signal` is the escape valve).
4. Replay system (seed + op log + event stream already carry everything).
5. Effect-aware bot policies (the `IBotPolicy` seam).
6. Advisory conflict-resolution UI (replaces the phase-1 gate).
7. Mod version pinning / float policy once distribution exists.
8. Full-fidelity cross-restart match resume (new engine state is data;
   persistence remains the missing half).
9. Stats semantics for modded matches (generic counters design).
10. AdminPanel + trusted-role auto-approve for UGC (dreamy-plan decisions,
    restated when workshop lands).

---

*End of spec. Owner: Eldyn. Brainstorm session 2026-09-19. Decisions Q1-Q26
on record in section 4; section 10.11 is the expressibility proof obligation
for the content authoring work package.*
