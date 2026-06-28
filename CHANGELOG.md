# Changelog

All notable changes to UNI are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/). The
`VERSION` file at the repo root is the single source of truth for the current
version; each release below corresponds to a `vX.Y.Z` git tag.

## [Unreleased]

## [0.4.8] - 2026-07-07

### Changed

- **Production deploys now gated on release tags**: CI still builds a multi-arch image on every push to `main`, but tags it `:edge` (+ `:sha-*`) instead of `:latest`. Only pushing a `vX.Y.Z` git tag publishes `:latest` (plus the matching semver tag), which is what the OCI deploy-watcher polls — so `main` can accumulate unreleased work without it shipping to playuni.app, and a release is cut simply by tagging.
- **Canonical domain moved to `playuni.app`**: All canonical/OG/structured-data URLs, `sitemap.xml`, `robots.txt`, and the GA4 `cookie_domain` now point at `https://playuni.app` instead of `https://unii.duckdns.org`. Traefik (in `uni-infra`) serves the new apex as primary and 301-redirects `www.playuni.app` and the legacy DuckDNS host to it, keeping `/.well-known/` answering 200 on the legacy host so the existing Bluesky atproto handle verification keeps resolving until the handle is re-verified against the new domain.

### Added

- **Bluesky handle verification file**: `frontend/public/.well-known/atproto-did` serves the account DID so the Bluesky handle can be domain-verified.

## [0.4.7] - 2026-06-28

### Added

- **Gameplay analytics events (GA4)**: A new `storeAnalytics` wrapper (`frontend/src/lib/stores/analytics.svelte.ts`) emits `gtag` custom events from the existing GA4 property. Instrumented points: `screen_view` (navigation), `sign_up`/`login`/`logout`/`auth_error` (auth), `lobby_create`/`lobby_join`/`lobby_leave`, `match_start` (host-only, carries the lobby ruleset — `active_mods`, `starting_cards`, `turn_time_limit_ms`, bot settings — as both broken-out params and a `settings_json` snapshot, excluding the unused `count_*` deck fields), `match_end` (host-only, `duration_seconds`), in-game `play_card`/`draw_card`/`call_uno`, `ws_reconnect`, and `server_error`. Both `match_start` (host's `startMatch()`) and `match_end` (host check in the `MatchOver` handler) fire only on the host, so match, rule-usage, and outcome counts are not inflated by per-client duplication (`MatchOver` broadcasts to every client). The wrapper no-ops safely when `gtag` is absent (ad blockers, SSR). Collected data describes gameplay only — no message content or personal data — and is used solely for game research.

### Fixed

- **Sitemap 404 on HEAD requests**: The server now registers an HTTP HEAD handler for static files alongside the existing GET handler. uWebSockets does not derive HEAD from GET automatically, so `curl -I` (and Google Search Console's sitemap validator) was receiving a 404 even though the file existed. The sitemap is also renamed from `sitemap-index.xml` to `sitemap.xml`, `robots.txt` updated to match, and `.xml` files are now served with `Content-Type: application/xml`.

### Changed

- **Icons migrated to HackerNoon Pixel Icon Library**: The remaining JetBrains Mono Nerd Font (MDI) glyphs across `GameUnoButton`, `FormInput`, `Toast`, `StatsScreen`, `DetailedStatsScreen`, `LobbyScreen`, and `LobbySettings` are now rendered as `<i class="hn pix hn-…">` pixel icons, matching the convention already used in the lobby browse UI.
- **`--mono` font stack**: Dropped the bundled `JetBrainsMono.woff2` (and its `@font-face`) now that it no longer provides icon glyphs. `--mono` resolves to a standard cross-platform monospace stack (`ui-monospace, "SF Mono", Menlo, Consolas, "DejaVu Sans Mono", "Liberation Mono", monospace`).

### Removed

- **Lobby connection-status icon**: The per-player connected/disconnected glyph is gone; disconnection will be conveyed by morphing the player's avatar instead. The dead `.nf-icon` optical-centering rule was also removed.

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
- **Action input stall**: A dedicated per-action timer fires in all bot modes when `IsWaitingForInput()` is true, keyed on `GetPendingPlayer()`. Colour picks and other mid-turn inputs are now auto-handled by a bot after the turn time limit expires — previously this stalled indefinitely in `kPlayInstantly` lobbies.

## [0.4.4] - 2026-06-26

### Changed

- **Wild → White/Jolly**: Wild cards renamed throughout — `Type::kWhite` (was `kWild`), `Value::kJolly` / `kJollyDraw4` (was `kWild` / `kWildDraw4`). Assets, CSS variables, and wire protocol updated to match.
- **game → match**: All internal module names renamed — directories (`include/match/`, `src/match/`), C++ symbols (`MatchState`, `MatchController`, `MatchRule`, `namespace match`), and WebSocket actions (`match_play_card`, `match_state_updated`, etc.).
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
  `serialization_test.cpp`, `lobby_controller_test.cpp` — engine, rules,
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
- Asset licensing clarified — CC0 scoped to project work, with a trademark
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

[unreleased]: https://github.com/Eldyn/uni/compare/v0.4.8...HEAD
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
