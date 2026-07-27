/**
 * @file devMatch.ts
 * @brief Local-only entry point that boots straight into a rendered match.
 * Exists so a browser agent can screenshot the board for any player count
 * without walking login -> guest -> lobby -> settings -> start every time.
 * Reached via `?dev=match&players=N`; the whole module is dynamically imported
 * behind `import.meta.env.DEV` in App.svelte, so it never ships in a
 * production bundle.
 */

import { storeAuth } from "$stores/auth.svelte";
import { storeGame } from "$stores/game.svelte";
import { storeNavigation } from "$stores/navigation.svelte";
import { pointerMode } from "$components/game/layout/pointerMode.svelte";
import { devFixturePreset } from "./devFixturePreset.svelte";
import { buildMatchFixture, parseMatchFixtureQuery, FIXTURE_LOCAL_USERNAME } from "./matchFixture";

/**
 * @brief Seeds the stores with a synthetic match and jumps to the game screen.
 * @param search The page's `location.search`.
 * @returns True when the harness took over, in which case the caller must skip
 *          the normal session check and WebSocket connect — the board is meant
 *          to render fully offline, and a live socket would overwrite the
 *          fixture on its first broadcast.
 */
export function tryStartDevMatch(search: string): boolean {
	const options = parseMatchFixtureQuery(search);
	if (options === null) return false;

	// `localPlayer` is derived from the auth username, so the fixture's local
	// seat only resolves once the store agrees on who the local player is.
	storeAuth.username = FIXTURE_LOCAL_USERNAME;
	storeAuth.isGuest = true;

	storeGame.state = buildMatchFixture(options);
	storeNavigation.goto("game");

	// `&pointer=touch|mouse` pins the gesture model. An automated browser always
	// reports a fine pointer, so without this the two-step touch play can't be
	// captured at all, however small you make the viewport.
	const pointer = new URLSearchParams(search).get("pointer");
	if (pointer === "touch" || pointer === "mouse") pointerMode.force(pointer === "mouse");

	// `&select=`/`&hover=` pre-arm a hand card so the halo, pulsing discard
	// target, and hover-lift are screenshottable without a live gesture.
	const hand = storeGame.localPlayer?.hand ?? [];
	devFixturePreset.selectId = options.select !== null ? (hand[options.select]?.id ?? null) : null;
	devFixturePreset.hoverId = options.hover !== null ? (hand[options.hover]?.id ?? null) : null;

	// A marker the browser agent can wait on before screenshotting, rather than
	// guessing at a fixed delay.
	document.documentElement.dataset.devMatch = String(options.players);

	// Exposed for the screenshot harness to force specific states (e.g. an
	// opponent's card_count) that the fixture's own randomized spread doesn't
	// reach on its own.
	(window as unknown as { __uniDevStore: typeof storeGame }).__uniDevStore = storeGame;

	console.info("[dev-match] fixture active", options);
	return true;
}
