/**
 * @file navigation.svelte.ts
 * @brief Manager of the application's internal routing (Single Page Application).
 * Mirrors navigation onto `window.history` so the browser/OS back gesture
 * (including mobile swipe-back) drives the "Back" action.
 */

import { storeAnalytics } from "./analytics.svelte";
import { storeAuth } from "./auth.svelte";
import { storeGame } from "./game.svelte";
import { storeLobby } from "./lobby.svelte";
import { ws } from "./ws.svelte";

/**
 * @typedef AppScreen
 * @brief List of the screens available in the frontend application.
 */
export type AppScreen =
	| "main"
	| "lobbies"
	| "lobby"
	| "game"
	| "settings"
	| "stats"
	| "detailedStats";

/**
 * @typedef HistoryState
 * @brief Shape of the object pushed to `window.history` on every navigation,
 * so the browser/OS back gesture can restore it via `popstate`.
 */
interface HistoryState {
	screen: AppScreen;
	authModalOpen: boolean;
	authTab: "login" | "register";
}

/**
 * @brief Per-screen validity checks, keyed by screen name.
 * A screen with no entry here is always valid (e.g. "main", "lobbies",
 * "settings" don't depend on any transient state). Screens absent from this
 * map are backed by state that a stale `window.history` entry can outlive
 * (the lobby/match/session may have ended since that entry was pushed), so
 * a back/forward gesture landing on one must be re-checked against the
 * live store before it's actually applied.
 */
const SCREEN_GUARDS: Partial<Record<AppScreen, () => boolean>> = {
	lobbies: () => storeAuth.isLoggedIn || storeAuth.isGuest,
	lobby: () => storeLobby.isInLobby,
	game: () => storeGame.state !== null,
	stats: () => storeAuth.isLoggedIn || storeAuth.isGuest,
	detailedStats: () => storeAuth.isLoggedIn || storeAuth.isGuest
};

/**
 * @class StoreNavigation
 * @brief Reactive store for screen switching.
 * Uses localStorage to persist the current screen and restore it
 * after a reload (F5), after verifying the login state. Also mirrors every
 * navigation onto `window.history`, so the hardware/gesture back button
 * (mobile Chrome/Safari swipe-back included) moves within the app instead
 * of leaving the page, as long as there's an in-app screen left to return to.
 * @tag FRONT-NAV-001
 */
class StoreNavigation {
	/** The screen currently displayed to the user. */
	current = $state<AppScreen>("main");

	/** Whether the AuthScreen modal is open, overlaid on top of whatever screen is current. */
	isAuthModalOpen = $state(false);

	/** Which tab the AuthScreen modal should open on. */
	authTab = $state<"login" | "register">("login");

	#screenRestored = false;

	/** Coarse, non-identifying account bucket for analytics segmentation. */
	get #accountType(): "registered" | "guest" | "anonymous" {
		if (storeAuth.isLoggedIn) return "registered";
		if (storeAuth.isGuest) return "guest";
		return "anonymous";
	}

	constructor() {
		// Seed the entry the browser already loaded us on with our state shape,
		// instead of leaving it `null`. Without this, the first back gesture
		// after any in-app navigation lands on that `null` entry, we'd have
		// nothing to restore from and the *next* back would skip straight past
		// the app (closing the tab/going to the real previous page).
		window.history.replaceState(this.#historyState, "");
		window.addEventListener("popstate", this.#onPopState);

		ws.onOpen(() => {
			if (this.#screenRestored) return;
			this.#screenRestored = true;

			const localScreen = localStorage.getItem("currentScreen");
			if (!localScreen) return;
			if (!storeAuth.isLoggedIn) {
				localStorage.removeItem("currentScreen");
				return;
			}

			this.goto(localScreen as AppScreen);
		});
	}

	get #historyState(): HistoryState {
		return { screen: this.current, authModalOpen: this.isAuthModalOpen, authTab: this.authTab };
	}

	/** Restores a screen/modal state popped off `window.history` by a back or
	 *  forward gesture, after re-validating it against live app state. */
	#onPopState = (event: PopStateEvent): void => {
		const state = event.state as HistoryState | null;
		if (!state) return;

		const from = this.current;
		const to = state.screen;

		// Backing out of an in-progress match isn't a plain screen swap, it's
		// the same exit a player triggers by pressing "Leave"/"Return to
		// Lobby": the match is still tracked server-side (bots/other players,
		// quit_deletes_match/save_state rules) and needs the real leave flow,
		// not just a client-side jump to whatever the game screen sits on top
		// of in the lobby/lobbies stack.
		if (from === "game" && to === "lobby") {
			if (storeGame.state !== null) {
				storeLobby.leave();
			} else {
				storeGame.returnToLobby();
			}
			return;
		}

		// The popped-to screen's backing state (lobby membership, an active
		// match, an auth session) may have gone away since this history entry
		// was pushed. Landing on it anyway would render a broken screen, so
		// disregard the gesture: re-assert the current entry, cancelling the
		// browser's own pop, instead of applying an invalid destination.
		const guard = SCREEN_GUARDS[to];
		if (guard && !guard()) {
			window.history.pushState(this.#historyState, "");
			return;
		}

		this.current = to;
		this.isAuthModalOpen = state.authModalOpen;
		this.authTab = state.authTab;
		localStorage.setItem("currentScreen", to);
	};

	/**
	 * @brief Changes the current screen.
	 * Automatically saves the screen to `localStorage` and pushes a new
	 * `window.history` entry so the back gesture can return here.
	 * @param screen The new destination screen.
	 */
	goto(screen: AppScreen): void {
		if (screen === this.current) return;
		this.current = screen;
		localStorage.setItem("currentScreen", screen);
		storeAnalytics.track("screen_view", { screen, account_type: this.#accountType });
		window.history.pushState(this.#historyState, "");
	}

	/**
	 * @brief Opens the auth modal on top of the current screen.
	 * Pushed as its own `window.history` entry, so a back gesture closes the
	 * modal instead of leaving the screen underneath it.
	 * @param tab Tab the modal should start on. Defaults to "login".
	 */
	gotoAuth(tab: "login" | "register" = "login"): void {
		this.authTab = tab;
		this.isAuthModalOpen = true;
		window.history.pushState(this.#historyState, "");
	}

	/**
	 * @brief Closes the auth modal, leaving the current screen untouched.
	 * Replaces (rather than pushes) the `window.history` entry `gotoAuth`
	 * pushed to open it, so a later back gesture returns to whatever was
	 * current before the modal opened instead of re-opening it.
	 */
	closeAuthModal(): void {
		if (!this.isAuthModalOpen) return;
		this.isAuthModalOpen = false;
		window.history.replaceState(this.#historyState, "");
	}

	/**
	 * @brief Returns to the previous screen, mirroring the back gesture.
	 */
	back(): void {
		window.history.back();
	}
}

export const storeNavigation = new StoreNavigation();
