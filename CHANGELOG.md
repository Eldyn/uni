# Changelog

All notable changes to UNI are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/). The
`VERSION` file at the repo root is the single source of truth for the current
version; each release below corresponds to a `vX.Y.Z` git tag.

## [Unreleased]

### Added

- **Match-start cinematic state plumbing**: a new reactive `storeMatchIntro` (`matchIntro.svelte.ts`) holds the board overrides a match-start deal sequence needs — `active`, `drawPileCount`, `drawPilePos`, `discardHidden` and `forcePurpleMat`, driven by `begin()`/`end()` — and `storeGame` now exposes `matchIntroPending`, set when a fresh `match_start` frame arrives and cleared by `reset()`/`returnToLobby()`. Foundation only; no component consumes it yet.
- **Pure match-start deal plan**: `animation/dealPlan.ts` turns the opening snapshot into the deal cinematic — `dealOrder` seats opponents in turn order with the POV player last, `startingHandCount`/`dealStaggerFor` pick the per-player count and departure pace, and `buildDealBeat` plans every card's flight from the centre pile as one skippable beat (locals flip face-up then move, opponents stay covered) and returns the world-space anchors each move targets. No Svelte/GSAP/DOM imports; it is the testable core the match-intro controller will drive.
- **Public online-player count endpoint**: `GET /stats/online` returns `{"online": <n>}`, the live connected-player count from `PresenceRegistry` (unique usernames, aggregate only — no PII), served `no-store` and behind the shared per-IP HTTP limiter. The blog's player-count pill (`playuni.app/blog`) polls it; until this shipped the pill stayed hidden.
- **Configurable per-lobby player cap**: `LobbySettings.max_players` (default 4, sanitized to `[2, ABSOLUTE_MAX_LOBBY_MEMBERS]`) replaces the flat compile-time `contract::kMaxLobbyMembers` check in `Lobby::AddOrHijack` and bot-sync; the contract ceiling itself was raised and is now broadcast to clients instead of assumed. A new `ABSOLUTE_MAX_LOBBY_MEMBERS` env var (defaulting to the contract ceiling, 16) sets the absolute upper bound.
- **Starting-hand/deck-size safeguard**: `LobbySettings::Sanitize()` now clamps `starting_cards` down if `starting_cards * max_players` would exceed the generated deck size, so large lobbies can't be configured into an unwinnable deal.
- **Proportional browse-screen occupancy gauge**: `OccupancyGauge.svelte` always renders 4 icons regardless of `max_players`, each clipped to its fractional share (star-rating style), replacing the old one-icon-per-seat rendering that stopped scaling past 4 seats. A "Max players" slider was added to `LobbySettings.svelte`.
- **N-player seat layout engine**: `layout/seatLayout.ts` (pure, unit-tested) computes per-seat position/scale/rotation for any opponent count — an elliptical ring on desktop/landscape, two vertical rails on mobile/portrait — replacing `GameBoard.svelte`'s hardcoded `LAYOUT_LEFT/TOP/RIGHT` constants and 1/2/3-length branching. `useViewport.svelte.ts` and `game-layout-context.svelte.ts` track viewport size/orientation to drive it.
- **Unified `PlayerSeat.svelte`**: replaces `OpponentHand.svelte` and the inline local-player markup with one component (avatar box, name label, turn highlight, target-picker affordance) shared by every seat; the local player's interactive hand is slotted in via a Svelte snippet instead of a separate component per role.
- **Turn-order strip**: `TurnOrderStrip.svelte` shows up to 2 players before/after whoever's turn it currently is (direction-aware, via the new pure `computeTurnOrderWindow()` helper), and the game HUD is now collapsible.
- **Discard pixel-shadow + draw pile relocation**: the discard pile's top card now renders with an offset flat-silhouette duplicate behind it for a pixel-art drop shadow (no new art asset, `DiscardPile.svelte`); the draw pile moved out to sit beside the local hand (`DrawPile.svelte`) instead of centered together with the discard.
- **Real accumulating discard pile**: the server only ever sends the single `top_card`, so the pile of dropped cards is now built client-side (`layout/discardPile.ts`, pure + unit-tested) — each newly-revealed top card is appended with a rotation and jitter fixed by its id (stable across re-renders), the history is capped (`DISCARD_CAP`) with the oldest evicted first, and `DiscardPile.svelte` renders the whole scatter, newest on top, instead of a single swapped card.
- **Opponent hands scale smoothly from an arc to a full ring**: a single continuous formula (`layout/handRing.ts`, pure + unit-tested) — a fixed step angle between adjacent cards that saturates once the spread would exceed a full turn — takes a lone card straight from "held in front of the player, facing the table" through a widening arc to a fully closed circle at large hand sizes (~20 cards), with no branching on card count.
- **Seat cross for 1–3 opponents**: `seatLayout.ts` now seats any small table (≤ 3 opponents) at exact compass points — 1 at due-top, 2 across left/right, 3 as the existing right/top/left cross — via a per-count angle table, reading as "across the table" rather than an arc approximation. The bare ring angles are single-sourced through `computeSeatAngles()`.
- **World-space board geometry foundation**: pure, renderer-agnostic seat and hand modules for the Threlte/WebGL board — `layout/seatLayout3D.ts` projects the same seat angles onto a circular XZ ring (unit-tested), and `layout/handLine.ts` holds the local hand's straight-row geometry. Threlte (`@threlte/core`, `@threlte/extras`, `three`) added as dependencies.
- **The entire board — including your own hand — is now a real, top-down Threlte/WebGL scene**: `three/Scene3D.svelte` (hosted by a `<Canvas>` in `GameBoard.svelte`) replaces every DOM card with actual meshes — opponents (`three/PlayerSeat3D.svelte`: avatar + `computeHandRingSlots` card-back ring + name label beyond the ring's edge), the local player (`three/LocalSeat3D.svelte` + `three/LocalHand3D.svelte`, a straight, slightly-overlapping row via `layout/handLine.ts`, click-to-play and drag-to-reorder), and both piles (`three/DrawPile3D.svelte`, anchored just left of the local hand on the same line, 1.25x scale; `three/DiscardPile3D.svelte`, rendering the full `discardHistory` scatter at 1.5x scale with no decorative base card). `three/CardMesh3D.svelte` ports `GameCard.svelte`'s layered background/value/border texture recipe to flat, top-down WebGL planes — no perspective tilt anywhere, cards included — and adds a hover lift (rise + full reveal over an overlapping neighbor) for interactive hands. The camera is a straight top-down orthographic view: `layout/designGrid.ts` (pure, unit-tested) defines the board's canonical row/column coverage — including a margin beyond the opponent ring so a seat's card ring and name label are never clipped at the frustum edge — and `layout/cameraRig.ts` fits it to the viewport's exact aspect ratio (no letterboxing, no distortion) so the ring and local hand stay fully on screen from narrow phones to wide desktops. The DOM `PlayerSeat.svelte`/`PlayerHand.svelte`/`SortableCardSlot.svelte` (and the `@dnd-kit` drag-reorder library they used) are removed along with the CSS perspective-tilt hack on `GameBoard.svelte`.

- **Two-step play: pick a card, then tap the discard pile to confirm**: tapping a card in your hand selects it (lifted, scaled and rimmed with a card-shaped halo, `CardHighlight3D.svelte`) instead of playing it immediately, and the discard pile's top card grows its own pulsing halo plus an oversized tap plane as the confirm target (`DiscardPile3D.svelte`). `Scene3D.svelte` owns the selection and clears it when the turn ends or the card leaves your hand. A phone has no hover to preview a play with, and a mis-tap in a card game is unrecoverable.
- **The local hand is a scrollable strip when it outgrows the screen**: `computeHandLine` (`layout/handLine.ts`, rewritten + unit-tested) stops compressing at a minimum spacing and returns a scroll range instead; dragging any unselected card pans the row, dragging the selected one still reorders it. Cards ramp their material opacity down over the last two ems at each end (`CardMesh3D`'s new `opacity` prop) so the row dissolves rather than being sliced off at the frustum edge.
- **Gzip compression for every static asset**: the frontend build writes a `.gz` sidecar next to each compressible file (`frontend/scripts/gzip-assets.js`, wired in as a Vite plugin), and the server serves it whenever the client sends `Accept-Encoding: gzip` (`http::AcceptsGzip` / `http::PrecompressedVariant`, with `Vary: Accept-Encoding` and a per-variant ETag). Compression happens once at build time at maximum level, so no request costs any CPU. The match bundle, which carries three.js/Threlte, drops from 837 kB to 224 kB on the wire; across the whole build it is ~2.4 MB down to ~600 kB. `.wasm`, `.json` and `.webmanifest` also gained proper MIME types, the draco/basis decoders three.js streams in need an exact `application/wasm`.
- **Local screenshot harness for the match board**: `?dev=match&players=N` boots straight into a synthetic, fully offline match — no login, lobby or match start — so the board can be captured at any player count (`frontend/src/lib/dev/matchFixture.ts`, deterministic and unit-tested; `dev/devMatch.ts` seeds the stores). `scripts/screenshot-match.sh` drives it through `agent-browser` across a viewport × player-count matrix. The whole thing sits behind the `__DEV_HARNESS__` build constant, a literal `false` in a production build, so it is dropped from the shipped bundle.
- **Deck picker in the lobby**: the lobby now lists the server's deck catalogue (`GET /api/decks`) and lets the host pick which deck a match loads, instead of every match being locked to the vanilla ruleset. A deck carries its own cards, mods and settings, so choosing one is what selects the rules.
- **Server-driven match animations**: the server now emits an ordered event stream for everything that happens in a match, and the board plays its card flights, reveals and turn changes from those packets rather than guessing by diffing state snapshots, so animations stay in order and in sync with the authoritative game.
- **`@choose_player` prompt sugar, and a modded card that uses it**: a behavior graph can now name `@choose_player` as an op selector and the engine opens a `choose_player` prompt for the acting seat, then binds the chosen seat on the answer — so a card can target whoever its player picks. Seven-Zero's 7-swap now reproduces its legacy outcome exactly (the two golden-replay known gaps it owned are closed). Ships `mods/test`: a **Bomb** card (pick a colour, then a seat → that seat draws two) and a 10-copy "Classic + Bombs" deck.
- **Mod asset pipeline and modded card faces**: mods now ship content in entry-set directories (`cards/`, `rules/`, `statuses/`, `mutations/`, `decks/`) plus per-mod asset bundles (`assets/<bundle>/index.json`), and `mod.json` is metadata only. Every mod is validated in full before it loads — errors block the mod with a readable per-mod report at startup and via `GET /api/mods`, warnings never do — and assets are served by id + content hash (`GET /assets/<mod>/<bundle>/<slot>/<tier>/<hash>`) with an immutable cache header, never exposing a filesystem path. Card faces gain `image` art with `inset`/`replace`/`overlay` composition, tiered variants (`high`/`medium`/`low`) and an always-terminating fallback chain (art → bundle emoji → face glyph → procedural → blank); a persisted presentation setting (`auto` follows device capability and reduced motion) picks the tier ceiling. `mods/test`'s Bomb now renders a real sprite inset in the card, and a new emoji-only wild card exercises the no-art path.
- **Password reset via magic link**: `POST /auth/reset/request` always answers `202` (silent over-cap, unknown-email no-op) and emails a single-use, 60-minute opaque token (32 CSPRNG bytes, SHA-256-hex at rest, new `password_reset_tokens` table / migration v7); `POST /auth/reset/confirm` consumes it, applies the same password policy as registration, sets `email_verified = 1` and auto-logs-in with the usual `auth_token`/`ws_token` cookies. New client-only `/reset-password/<token>` route opens a reset modal (`storeReset`, `ResetPasswordForm`, `ResetModal`), reachable from a "Forgot password?" link on the login form, and the token/reaper/send-log/hashing paths are shared with email verification. Session revocation on reset is a documented, accepted gap.
- **Match-start board gating**: the deal cinematic's `storeMatchIntro` now drives the board — `DrawPile3D` reads its `drawPileCount`/`drawPilePos` overrides everywhere it places the pile and refuses drawing (hover cue and tap) while `active`, `Playmat3D` holds the felt at rebeccapurple (`#663399`) while `forcePurpleMat`, `DiscardPile3D` removes and plants no registry entries while `discardHidden`, and `matchEventController.syncState` defers the initial discard-top seed until the intro ends.
- **Match-start deal cinematic**: `animation/matchIntroController.svelte.ts` drives the opening deal end to end — it seeds every already-dealt card at a centre draw pile, plays one staggered, skippable beat that flies each card to its seat (opponents covered, the POV hand flipping face-up), slides the pile to its real home, then reveals the first discard on the pile. `GameBoard.svelte` starts it once per fresh `match_start` from `storeGame.matchIntroPending`, a board tap while it runs calls `skip()` to fast-forward, and `?dev=match&intro=1` exercises it offline. Honors reduced motion by skipping straight to the normal opening board.
### Changed

- **Deeper draw-pile hover dip**: the draw pile's top card now slides further toward the player while hovered with a draw available (`HOVER_DIP_Z` 0.06 → 0.14, `HOVER_DIP_DURATION_S` 0.15 → 0.18), so the "take the card" cue — and the drawn card's flight, which is seeded from that exact offset pose — reads more clearly.
- **Turned card-backs are non-interactive**: face-down cards (opponent rings, pile backs) drop their button role, focusability, click/keyboard handlers and hover lift, and cards render with `image-rendering: pixelated` so the pixel-art stays crisp when scaled.
- **Per-player avatar tint replaces the color halo**: human players' avatars are now color-multiplied (`mix-blend-mode: multiply`) instead of wrapped in a `drop-shadow` glow, so a seat reads its owner's color directly on the avatar itself; bots keep their plain look. The turn indicator's own glow is unchanged.
- **The 3D board fills the whole screen and the HUD floats on top**: `GameScreen.svelte`'s HUD row (`.game-controls`) no longer stacks above the board in the flex flow — it's an absolutely-positioned overlay (click-through outside its widgets), and `.game-board-container` owns the full viewport, so the scene's center no longer drifts below the true screen center by half the HUD's height.
- **Opponents sit on an ellipse, not a circle**: `seatLayout3D.ts` now projects the shared seat angles onto per-orientation ellipse radii — wide and shallow in landscape (`8 × 4.2` world units) so seats spread around the playmat with more breathing room, narrow and deep in portrait (`4 × 5.5`) so the rails run down the phone's long axis. `designGrid.ts`'s board extents (`boardExtentsFor`) derive from the same radii per orientation instead of the old fixed `GRID_COLUMNS`/`GRID_ROWS` constants.
- **Cards render at the old DOM board's size again**: the shallower ellipse plus a pulled-in local seat line (`LOCAL_SEAT_Z` 5.5 → 4.2) shrink the board's reference grid, so the "contain" camera fit renders every world unit ~25% larger — at 1920×1080 with 4 players a card is back to roughly the old `--cardSize: 5em` footprint, still scaling down proportionally on smaller viewports. The local hand additionally renders at 1.35× the world card baseline (spacing and hover push scaled with it), matching how the old board drew your own readable cards larger than opponents'.
- **Opponents form a reversed-U arch instead of a near-closed ring**: `seatLayout.ts` caps the seat arc's half-span per orientation (landscape 126°, portrait 118°, replacing the shared 155° max that let the lowest seats sink beside — and clip — the local hand), and `seatLayout3D.ts` maps the X coordinate through a superellipse (`|cos|^(2/exp)`, exponent 2.5 landscape / 5 portrait) so bottom seats push outward toward the ring's full width — curved across the top, near-vertical sides. The landscape ring also widens (`rx` 8 → up to 9.4) as the table fills, using the 16:9 slack instead of crowding seats inward.
- **Opponent seats shrink as the table fills**: `Scene3D.svelte` scales each seat's ring cards (0.55 → 0.4), avatar (70px → 56px) and name label down smoothly with opponent count on landscape; on portrait the card fan stays full-size while the avatar drops to a 36px marker and the label to 0.65em — the fan, not the icon, is the seat's focus on a phone.
- **The board pins to the bottom of a too-tall frustum**: when the viewport is width-constrained (portrait phones), `designGrid.ts` slides the frustum down until the board sits `BOTTOM_PIN_MARGIN` above the screen's bottom edge instead of floating centered — the local hand no longer strands mid-screen with dead space below it. Portrait also uses a tighter ring reach (1.6 vs 2.2 world units, matching its smaller avatars/labels) and a wider, deeper ring (`4.6 × 6.5`), so seats use the phone's edges instead of huddling around the center.
- **The draw pile reads as an actual pile, at hand-card size**: `DrawPile3D.svelte` renders its backs at the shared `HAND_SCALE` (1.35, was 1.25) and each deeper back peeks out slightly below the one above, instead of six backs perfectly hidden under one card. The pile moved left (`DRAW_PILE_X` −4.6) and the local hand's row span is capped against it (`computeHandLineSlots`' new `maxHalfSpanEm`): a growing hand compresses its overlap instead of flowing under the pile.
- **Discard scatter is wilder and every card drops a shadow**: scatter bounds grew (rotation ±16° → ±30°, jitter ±0.5 → ±1.1em) and the seed now mixes in the pile `seq`, so the same card lands differently each time it cycles back. `DiscardPile3D.svelte` renders a black, semi-transparent silhouette (the card background texture) offset toward the bottom-right under every card, so stacked same-color cards stop merging into one blob.
- **Opponent ring cards sit a touch farther from their avatar**: `handRing.ts`'s `RING_RADIUS_EM` 4.5 → 5.
- **Opponent seats are spaced evenly along the arch, not evenly in angle**: the world arch is far wider than it is deep, so a degree of angle bought roughly twice as much travel across its top as down its near-vertical sides — the seats nearest due-left and due-right stacked almost on top of each other. `seatLayout.ts` now accepts an `ArcWarp`, and `seatLayout3D.ts` supplies one built from a cumulative arc-length table of the ring's own curve, so neighbouring seats sit the same distance apart wherever they are on the arch.
- **The board is centered on the screen and the hand hangs off its bottom edge**: `designGrid.ts` now sizes the frustum from the opponent ring alone and keeps the world origin at the exact center of the screen on every aspect ratio, so the playmat and the discard pile are always dead center. The local hand isn't part of those extents at all — the new `layout/boardPlacement.ts` (pure, unit-tested) derives the hand row's Z, its scale and the draw pile's X *from* the resulting frustum, dropping the row one 0.3-unit margin inside whatever bottom edge the viewport produced. `LocalSeat3D.svelte` puts the avatar and name above the row (cards under that), and a hovered card pops out *away* from the viewer, over the mat, since there is no screen left below the row.
- **Smaller opponents, unchanged local hand**: opponent ring cards (0.55 → 0.5 portrait, 0.42 at an empty landscape table down to 0.3 at a full one), avatars (70 → 46px, 35px at a full table) and name labels all shrank, so the ring reads as cards on a mat rather than a crowd of oversized icons. The local hand keeps its 1.35 base scale and the discard pile stays at `handScale × 1.1`.
- **The local hand grows as the screen gets narrower**: `boardPlacement.ts` scales the hand by `min(1.4, 1 / aspect)`, so a phone's tall, thin frustum — which makes any fixed world-space card read tiny — buys that size back, up to 1.4x. Landscape is unaffected.
- **The portrait seat ring matches the phone's own proportions**: the frustum is a "contain" fit, so a ring flatter than the screen's aspect made the horizontal axis binding and spent the difference as dead space above and below — every card on a phone rendered at roughly half the size it could have. Portrait `rx` 4.6 → 3.6, `rz` 6.5 → 7.6 and the ring's margin 1.6 → 1.3, shrinking the portrait frustum's half-height from ≈12.5 to ≈9.9 units.
- **The lobby ceiling is 16 players, not 15**: `contract/asyncapi.yaml`'s `LobbyMemberCap` maximum 15 → 16 (and `bot_count` 14 → 15, one below the cap so a lobby always keeps a human seat), regenerating `contract.hpp` and `schemas.ts`; `seatLayout.ts`'s `MAX_OPPONENTS` follows at 15.
- **Seat names are shown on demand instead of permanently**: at a full table a label per seat is a wall of text nothing on the board can outrank. `PlayerSeat3D.svelte` now hangs the name off the avatar's bottom edge as a badge and only fades it in when the seat is relevant — it's that player's turn, you're choosing them as a target, or you're pointing at them. The separate far-side label (and the side-of-ring justification logic it needed) is gone.
- **Names render in the small pixel font**: seat labels use `--tiny` (Habbo) instead of bold `--pixel`, and opponent names ellipsize rather than running across a neighbouring seat.
- **Discard shadows are shorter and fainter**: offset 0.16 → 0.09 world units, opacity 0.35 → 0.22, so a deep pile reads as loose cards on the mat instead of a tall tower.
- **Client IPs in logs and rate-limit keys are always plain IPv4**: `http::GetClientIp` (`include/common/http.hpp`) now unwraps IPv4-mapped IPv6 addresses (`::ffff:1.2.3.4` → `1.2.3.4`) before they're logged or used as a limiter/connection-cap key; genuine IPv6 addresses pass through unchanged. uWS binds dual-stack sockets, so IPv4 clients were previously logged in this mapped form.

- **The board is bigger on a phone**: the portrait seat ring's X radius (`PORTRAIT_RING_RX`) drops 3.6 → 3 and its reach allowance (`PORTRAIT_OPPONENT_RING_REACH`) 1.3 → 1.15. On a portrait phone the frustum is width-binding, so only those two shrink it; the result is roughly 15% larger cards and avatars. The local hand's narrow-screen boost ceiling (`MAX_HAND_BOOST`) also rises 1.4 → 1.6.
- **Your own seat is labelled "You"**: `LocalSeat3D.svelte` no longer prints your username under your own avatar — at a full table it was one more label competing with the fifteen that do matter.
- **Hover lift and scale are shared constants**: `CARD_HOVER_LIFT` and `CARD_HOVER_SCALE` moved into `three/units.ts` so the selection halo can be posed at exactly the lifted card's pose instead of guessing at it.
- **New red/green/blue/yellow palette, defined once**: the four game colours are now `#bd3130` / `#4aab42` / `#0470dd` / `#f2cc47`, each with a lighter companion shade (`#ca4a49` / `#79bd4c` / `#1790df` / `#f6d55e`). A new `src/lib/palette.ts` is the single source for the WebGL scene, `app.css` mirrors it as `--redCard`/`--redCardLight` etc. for the DOM, and card faces, the pick-a-colour prompt, seat identity colours and the victory popup all read from one of the two instead of carrying their own copies of the hexes.
- **The playmat and turn arrows tint from the shared palette**: `Playmat3D.svelte` carried its own copy of the old hexes for the multiply tint; it now reads `CARD_COLOR_MAP`, so the mat under the pile matches the card that put it there.
- **The wooden backdrop behind the board is gone**: `GameScreen.svelte` dropped the full-screen `background.png` and the four stale `--red`/`--yellow`/`--blue`/`--green` variables it defined alongside it (nothing referenced them). The board sits on the app's own background now.
- **Seat names are plain text again**: the dark plate and hairline outline behind every name label are gone, in both `PlayerSeat3D.svelte` and `LocalSeat3D.svelte`.
- **Data-driven card engine**: the match now runs on a general entity/component rules engine driven by deck and mod definitions, replacing the hardcoded vanilla rule, effect and rule-registry implementations. Cards, decks and rule mods are content the server assembles into a match, not compiled-in behaviour.

### Fixed

- **A response window that times out no longer stalls the match into a false AFK takeover**: the window's timeout tick closed the window and advanced the turn but never re-armed the turn driver, so the next seat got no bot/turn timer and the stale timer armed for a window responder later fired as "Bot playing for AFK player" against someone whose turn it was not. The timeout path now re-arms the driver for the new actor, as the pass/respond path already did.
- **Progressive no longer disarms itself and Force Play no longer force-plays penalty draws**: draw hooks now carry a `cause` (`action`, `effect`, `until_playable`) exposed to mods through the new `draw_cause` condition. Progressive only reacts to the player's voluntary draw, so its own draw-until-playable draws stop re-entering it (the `after:draw` re-entry cap tripped and the mod was disarmed); Force Play skips cards drawn by effects such as a +2, which used to queue a stale out-of-turn forced play.
- **Draw Stacking now stacks onto the next seat**: with `draw_stacking` active, +2/+4 cards no longer draw immediately (content mutations strip the draw/skip); the play records its N as `vanilla:draw_debt` on the seat after the player, a stacked response moves the accumulated debt on to the seat after the responder and hands the responder the turn, and when nobody answers the victim draws the whole debt and is skipped. Debt is recorded once per play even when another window (e.g. Jump-In) opens over it, and the `draw_debt` condition now reads the status magnitude.
- **Draw-stack "+N" indicator shows again**: the engine snapshot now carries `pending_draws` (the outstanding `vanilla:draw_debt` magnitude), which `DrawStackIndicator` reads; the ECS server never sent it, so the counter stayed hidden.
- **Window responders can see their playable cards**: while a response window is open the snapshot's `can_play` now follows the window (new `MatchInstance::CanRespondWindow`): a pending responder's eligible cards light up out of turn, and the player who opened the window no longer sees playable cards the server would reject.
- **Out-of-turn window replies work on the client**: a new `storeGame.isWindowResponder` keeps the board's card selection and the keyboard hand controls alive for a window responder instead of clearing/blocking them because it is not their turn, so a draw-stacking or jump-in reply can be picked and played; the pass chip and `passWindow` share the same check.
- **A player knocked out in an elimination match no longer stays a spectator into the next one**: `HandleStartGame` cleared the lobby's stale `is_spectator` flag but only broadcast match state, so each client kept the `true` cached on its copied member list and `storeGame.isSpectator` OR-ed that stale flag into the result — pinning a returning, now-seated player in spectator view (and blocking their actions). The start handler now broadcasts the refreshed lobby roster so every client drops the flag it no longer holds.
- **The discard pile's scatter and shadow scale with the cards**: the jitter offsets and drop shadow were sized for a full-size desktop card, so a smaller phone pile threw a detached black smear. Each now scales with the pile's own card size.
- **The discard pile sits at the center of the felt, and the draw pile stays beside your hand on phones**: portrait used to drop both piles into a centered cluster and push the draw pile away from the hand. The discard is now anchored to the felt's true midpoint and the draw pile stays beside the hand, as a smaller tap target.
- **Bigger cards in your hand on a phone**: portrait used to keep seven cards visible before the row started scrolling, which capped the card size on a narrow screen. It now keeps five, so cards render larger and you scroll sooner.
- **Opponents reach closer to the screen's edges on a phone**: at fuller tables the portrait arch now widens toward the left and right edges instead of staying pinned to its sparse-table width.
- **Mobile HUD**: the timer is pinned left and Exit pinned right, and the duplicate inline "SPECTATING" badge is gone.
- **The HUD row and spectator banner no longer overlap**: they now stack instead of both anchoring to the top edge on short screens.
- **Touch drag and hand-pan no longer get eaten by the browser**: touch-action is disabled on the game canvas, so reordering a card or panning a long hand works reliably on touch devices.
- **The board's scaling is no longer inverted**: opponent avatars and name labels were declared in CSS pixels while everything else on the table lived in world units. The camera zooms out as a viewport gets less wide-screen, shrinking every card — but a pixel-sized avatar held its exact on-screen size and so grew relative to the board around it. Both are now sized in world units and converted through a single `worldPerPx` factor at the end, so the seats give way as the screen tightens instead of the player's own cards doing it for them.
- **The discard pile no longer grows into the opponent ring**: the center pile rode on `handScale`, so the narrow-aspect boost that exists to keep the player's own cards readable inflated the pile too — into a ring whose depth was a hard constant and couldn't move out of the way. `BoardPlacement.centerScale` is now independent of that boost, and additionally shrinks (to a readable floor) if the nearest seat is genuinely close enough to collide.
- **The opponent arch fills vertical slack instead of leaving it above the seats**: `ringRadiiFor` grows the landscape ring's depth toward `LANDSCAPE_RING_RZ_MAX` the way it already grew its width, bounded by the reach a seat's own cards actually need. A squarer window used to pile its extra rows up as empty mat above the top seat while that seat stayed pinned on top of the pile.
- **Top opponents no longer clip off the screen**: `boardExtentsFor` sized the frustum at the fullest table only, but a sparse table's reach margin is twice a full one's (`ringRadiiFor`'s width grows with the count while `ringReachFor`'s margin shrinks), so a 4-player game needed more depth than had been reserved. The extents now cover every seat count. Relatedly, seat reach is measured to a ring card's corner rather than its edge — ring cards are spun to stay radial, so a wide fan turns some of them far enough that a corner is the outermost point, and the missing fifth of a card was exactly the sliver being clipped.
- **Cards no longer z-fight anywhere on the board**: each `CardMesh3D`'s own layered background/value/border planes sit up to 0.004 world units apart, and the per-card stacking step in the local hand, opponent card rings, discard pile and draw pile were all smaller than that, letting one card's layers interleave with its neighbor's. Every step is now 0.02, comfortably clear of the internal layer span.
- **Opponent avatars no longer overlap their own name label**: a center-anchored label of unbounded width grew back over the avatar for a long username. Avatar and name are now one stacked unit with the name capped at 9em and ellipsized, so a long name truncates instead of running over the avatar or across a neighbouring seat.
- **`designGrid.ts`'s column margin now matches its row margin**: `GRID_COLUMNS` only added a flat `+1.5` buffer beyond the opponent ring, while `GRID_ROWS` already added the fuller `OPPONENT_RING_REACH` margin (added last round for the top-seat clipping fix) — left/right seats' labels could still clip the frustum's left/right edge. Both axes now share the same margin.

- **Opponent ring cards fan around their own avatar, not toward the discard pile**: each ring card's spin negated its slot angle, leaving every card parallel to the seat's center line — the whole ring read as a cone aimed at the playmat center. Cards now spin by `slot angle + 180°`, keeping each card's long axis radial to the avatar (bottom edge toward it), so the ring reads as a hand fanned around its owner.
- **The local avatar's color tint no longer floods its whole box**: the `mix-blend-mode: multiply` tint layer painted the avatar img's transparent surroundings as a solid colored rounded box; the tint div is now CSS-masked to the avatar sprite's own pixels (`LocalSeat3D.svelte`, `PlayerSeat3D.svelte`), so only the icon itself takes the player color.
- **The HUD bar no longer clips off narrow screens**: on ≤700px viewports the HUD wraps and centers (`GameHud.svelte`, `GameScreen.svelte`) and the turn-order strip wraps its chips, instead of one long row pushing the Exit button out of the viewport.
- **Dragging a card no longer strands it in its hovered pose**: hover lived inside each `CardMesh3D` and was cleared by a `pointerleave` that a drag rarely delivers — the pointer ends the drag wherever it happens to be, often over no card at all. The hovered card is now picked by `LocalHand3D.svelte` and cleared explicitly when a drag starts and ends.
- **Hand cards are pickable across their whole width**: cards overlap, and the topmost one won every raycast, so selecting any other card meant hitting the few pixels of sliver showing past its neighbour. The hand now raycasts against invisible pick zones that tile the row edge-to-edge — each card owns everything from the midpoint with its left neighbour to the midpoint with its right one.
- **The playmat and the turn arrows are meshes in the scene, not CSS layers**: `three/Playmat3D.svelte` draws both as textured planes cover-fitted to the camera's frustum and tinted to the active color in WebGL (`MeshBasicMaterial` `map` + `color`, the PNG's own alpha acting as the mask), replacing the `.bg-layer` divs and the `--board-offset-y` custom property that used to keep the DOM art and the scene in sync. The card-flight anchors are still DOM, but are now projected from each pile's real world position relative to the (screen-centered) origin.
- **The draw pile no longer clips off the left edge on a phone**: its X was a fixed world constant, which sat outside the frustum once a narrow viewport shrank the visible width. `boardPlacement.ts` clamps it inward to `-(halfWidth − half a card − 0.2)` when it has to — one continuous formula, no orientation branch — and the hand's own maximum span is measured against the clamped position so the two can't overlap.
- **A played card can no longer land underneath the discard pile**: the pile's keyed `{#each}` used `card.id` as the key, but ids recycle when the discard is reshuffled into the draw pile — a replayed id still inside the capped 30-entry history collided with the old entry's key, reusing its low-in-the-stack render block so the "new" card appeared under the pile. Every `DiscardEntry` now carries a monotonic `seq` used as the render key.

- **The untinted sliver around player avatars**: `.avatar-box img` and its `.tint` overlay now both set `image-rendering: pixelated` (`LocalSeat3D.svelte`, `PlayerSeat3D.svelte`). The avatar is small pixel art upscaled to 30–56px, so smooth interpolation gave it a soft, partially-transparent edge — and multiplying through a mask with that same partial alpha only partly tinted it, leaving a visible fringe of untinted sprite.
- **The draw reshuffle no longer recycles the discard's top card**: the ops-layer reshuffle (`card_pile_ops.cpp` `EnsureDrawSource`, used by `draw_cards` and `draw_until_playable`) moved the *entire* discard into the draw pile, unlike `MatchInstance::ReshuffleDiscardIntoDraw`, which keeps the top. In a long game that could empty the discard completely — leaving no active pile to match and deadlocking the all-mods bot simulation. The ops path now keeps the top too.

### Security

- **Security hardening pass**: the WebSocket upgrade rejects any `Origin` not on the configured allowlist, closing cross-site WebSocket hijacking against the cookie-authenticated socket; JWT revocation on password reset and logout increments a per-user `users.token_version` counter, which `IssueToken` stamps into each token's `ver` claim and `VerifyToken` rejects on mismatch, so previously issued `auth_token`/`ws_token` cookies 401 immediately; startup fails closed unless `JWT_SECRET` (at least 32 chars), `PASSWORD_PEPPER` and, when `EMAIL_MODE=live`, `EMAIL_KEY` are set, and production requires `EMAIL_MODE=live` (the DM key is separately required by `Crypto::LoadKey`); user-facing responses at the Traefik edge carry `Strict-Transport-Security`, `X-Content-Type-Options: nosniff` and `X-Frame-Options: DENY`; DM ciphertext is bound to its sender/recipient through AES-GCM additional authenticated data; mod asset URLs are validated by parsing and pinned same-origin, with every served asset checked against its manifest content hash; and Conan dependencies are pinned through `conan.lock` (OpenSSL included), the container builds and runs as non-root, and the known transitive npm advisories are patched.

### Known issues

- **Per-seat player color stays a 4-color RGBY wraparound** rather than growing a richer palette past 4 seats, pending a future player-picked character color feature.

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

[unreleased]: https://github.com/Eldyn/uni/compare/v0.5.0...HEAD
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
