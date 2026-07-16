# Changelog

All notable changes to UNI are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/). The
`VERSION` file at the repo root is the single source of truth for the current
version; each release below corresponds to a `vX.Y.Z` git tag.

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
