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
	"main" | "lobbies" | "lobby" | "game" | "profile" | "stats" | "decks" | "shop" | "settings";

const SCREEN_PATHS: Record<AppScreen, string> = {
	main: "/",
	lobbies: "/browse",
	lobby: "/lobby",
	game: "/play",
	profile: "/profile",
	stats: "/profile/stats",
	decks: "/decks",
	shop: "/shop",
	settings: "/settings"
};

const PATH_SCREENS: Record<string, AppScreen> = Object.fromEntries(
	Object.entries(SCREEN_PATHS).map(([screen, path]) => [path, screen as AppScreen])
);

/** Maps a screen to the URL path it should be reachable at. */
export function pathForScreen(screen: AppScreen): string {
	return SCREEN_PATHS[screen];
}

/** Maps a URL path back to its screen, or null if it isn't one of ours. */
export function screenForPath(path: string): AppScreen | null {
	return PATH_SCREENS[path] ?? null;
}

/** Matches the share-link format produced by LobbyScreen's invite copy button. */
const INVITE_PATH = /^\/invite\/([A-Za-z0-9]{6})$/;

/**
 * Matches magic-link verification URLs.
 * /profile/verify/<code> is already served index.html by http::IsClientRoute
 * (src/common/http_utils.cpp:24-32) — no backend route change needed; don't add a redundant one.
 */
const VERIFY_PATH = /^\/profile\/verify\/(\d{6})$/;
/**
 * @typedef HistoryState
 * @brief Shape of the object pushed to `window.history` on every navigation,
 * so the browser/OS back gesture can restore it via `popstate`.
 */
interface HistoryState {
	screen: AppScreen;
	authModalOpen: boolean;
	authTab: "login" | "register";
	settingsOpen: boolean;
	verifyModalOpen?: boolean;
}

/**
 * @brief Per-screen validity checks, keyed by screen name.
 * A screen with no entry here is always valid (e.g. "main", "decks",
 * "shop" don't depend on any transient state). Screens absent from this
 * map are backed by state that a stale `window.history` entry can outlive
 * (the lobby/match/session may have ended since that entry was pushed), so
 * a back/forward gesture landing on one must be re-checked against the
 * live store before it's actually applied.
 */
const SCREEN_GUARDS: Partial<Record<AppScreen, () => boolean>> = {
	lobbies: () => storeAuth.isLoggedIn || storeAuth.isGuest,
	lobby: () => storeLobby.isInLobby,
	game: () => storeGame.state !== null,
	profile: () => storeAuth.isLoggedIn || storeAuth.isGuest,
	stats: () => storeAuth.isLoggedIn || storeAuth.isGuest
};

/**
 * Screens safe to restore from localStorage on next login. lobby and game
 * are deliberately excluded — the server re-establishes both on connect
 * (LobbyController::OnOpen resends lobby_joined and match state), and a
 * stale local guess could contradict that.
 */
const PERSISTED_SCREENS = new Set<AppScreen>([
	"main",
	"lobbies",
	"profile",
	"stats",
	"decks",
	"shop",
	"settings"
]);

/**
 * @class StoreNavigation
 * @brief Reactive store for screen switching.
 * Uses localStorage to persist the current screen and restore it
 * after a reload, after verifying the login state. Also mirrors every
 * navigation onto `window.history`, so the hardware/gesture back button
 * (mobile Chrome/Safari swipe-back included) moves within the app instead
 * of leaving the page, as long as there's an in-app screen left to return to.
 */
export class StoreNavigation {
	/** The screen currently displayed to the user. */
	current = $state<AppScreen>("main");

	/** The screen initially requested on page load / hard refresh. */
	initialScreen = $state<AppScreen | null>(null);

	/** Whether the AuthScreen modal is open, overlaid on top of whatever screen is current. */
	isAuthModalOpen = $state(false);

	/** Which tab the AuthScreen modal should open on. */
	authTab = $state<"login" | "register">("login");

	/** Whether the Settings modal is open, overlaid on top of whatever screen is current. */
	isSettingsOpen = $state(false);

	/** Whether the Verification modal is open, overlaid on top of whatever screen is current. */
	isVerifyModalOpen = $state(false);

	/**
	 * An invite code captured off a deep-linked `/invite/<code>` URL, waiting
	 * for a logged-in-or-guest session before it can be consumed (see App.svelte).
	 * Cleared once that consumer picks it up.
	 */
	pendingInviteCode = $state<string | null>(null);

	/**
	 * A 6-digit verification code captured off a deep-linked `/profile/verify/<code>` URL.
	 * Cleared once consumed by App.svelte.
	 */
	pendingVerifyCode = $state<string | null>(null);

	/**
	 * Verification code currently active for auto-submitting in ProfileScreen / VerifyCodeForm.
	 * Cleared once the form completes or is dismissed.
	 */
	activeVerifyCode = $state<string | null>(null);

	/**
	 * Set by HomeScreen's "+ Create Lobby" button right before navigating to
	 * "lobbies", so LobbyBrowse can pop its create modal open on arrival
	 * instead of the button just landing on a bare list. Consumed (and reset)
	 * by LobbyBrowse's own onMount.
	 */
	openCreateLobbyOnArrival = $state(false);

	#screenRestored = false;

	/** Coarse, non-identifying account bucket for analytics segmentation. */
	get #accountType(): "registered" | "guest" | "anonymous" {
		if (storeAuth.isLoggedIn) return "registered";
		if (storeAuth.isGuest) return "guest";
		return "anonymous";
	}

	constructor(initialPath?: string) {
		// A hard refresh/deep link lands the browser on a URL (e.g. `/browse`)
		// before this store exists. Resolve it back to a screen so the
		// `replaceState` below writes a URL that matches what's shown, instead
		// of unconditionally rewriting it to "/" and showing Home. Falls
		// through to the "main" default (silently) if the path isn't ours or
		// the resolved screen's guard rejects it — same stale-state guard the
		// back/forward gesture already goes through.
		const path = initialPath ?? (typeof window !== "undefined" ? window.location.pathname : "/");

		const inviteMatch = path.match(INVITE_PATH);
		if (inviteMatch) {
			this.pendingInviteCode = inviteMatch[1].toUpperCase();
		}

		const verifyMatch = path.match(VERIFY_PATH);
		if (verifyMatch) {
			this.pendingVerifyCode = verifyMatch[1];
		}

		const deepLinkedScreen = screenForPath(path);
		this.initialScreen = deepLinkedScreen;
		if (deepLinkedScreen) {
			const guard = SCREEN_GUARDS[deepLinkedScreen];
			if (!guard || guard()) {
				this.current = deepLinkedScreen;
			}
		}

		// Seed the entry the browser already loaded with the app's state shape,
		// instead of leaving it `null`. Without this, the first back gesture
		// after any in-app navigation lands on that `null` entry, the app would
		// have nothing to restore from and the *next* back would skip straight
		// past the app (closing the tab/going to the real previous page).
		if (typeof window !== "undefined") {
			window.history.replaceState(
				this.#historyState,
				"",
				pathForScreen(this.current) + (window.location?.search ?? "")
			);
			window.addEventListener("popstate", this.#onPopState);
		}

		ws.onOpen(() => {
			if (this.#screenRestored) return;
			this.#screenRestored = true;

			if (
				this.initialScreen === "game" ||
				this.initialScreen === "lobby" ||
				this.current === "game" ||
				this.current === "lobby" ||
				storeLobby.isInLobby ||
				storeGame.state !== null ||
				localStorage.getItem("lobby_code")
			) {
				return;
			}

			const localScreen = localStorage.getItem("currentScreen");
			if (!localScreen) return;
			if (!storeAuth.isLoggedIn) {
				localStorage.removeItem("currentScreen");
				return;
			}
			if (!PERSISTED_SCREENS.has(localScreen as AppScreen)) {
				localStorage.removeItem("currentScreen");
				return;
			}

			this.goto(localScreen as AppScreen);
		});
	}

	get #historyState(): HistoryState {
		return {
			screen: this.current,
			authModalOpen: this.isAuthModalOpen,
			authTab: this.authTab,
			settingsOpen: this.isSettingsOpen,
			verifyModalOpen: this.isVerifyModalOpen
		};
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
			window.history.pushState(this.#historyState, "", pathForScreen(this.current));
			return;
		}

		this.current = to;
		this.isAuthModalOpen = state.authModalOpen;
		this.authTab = state.authTab;
		this.isSettingsOpen = state.settingsOpen;
		this.isVerifyModalOpen = state.verifyModalOpen ?? false;
		if (PERSISTED_SCREENS.has(to)) {
			localStorage.setItem("currentScreen", to);
		}
	};

	/**
	 * @brief Changes the current screen, refusing when its backing state is
	 * absent.
	 *
	 * Applies the same SCREEN_GUARDS check #onPopState already runs on a
	 * back/forward gesture. Without this, a stale localStorage restore or a
	 * caller passing a screen the app isn't actually in (no active lobby, no
	 * live match) could land on a dead screen with no way out — the exact
	 * corrupted-relogin symptom this fixes.
	 * @returns True if the navigation was applied, false if a guard refused it.
	 */
	goto(screen: AppScreen): boolean {
		if (screen === this.current) return true;

		const guard = SCREEN_GUARDS[screen];
		if (guard && !guard()) return false;

		if (screen === "game") {
			this.closeVerifyModal();
		}

		this.current = screen;
		if (PERSISTED_SCREENS.has(screen)) {
			localStorage.setItem("currentScreen", screen);
		}
		storeAnalytics.track("screen_view", { screen, account_type: this.#accountType });
		window.history.pushState(this.#historyState, "", pathForScreen(this.current));
		return true;
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
		window.history.pushState(this.#historyState, "", pathForScreen(this.current));
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
		window.history.replaceState(this.#historyState, "", pathForScreen(this.current));
	}

	/**
	 * @brief Returns to the previous screen, mirroring the back gesture.
	 */
	back(): void {
		window.history.back();
	}

	/**
	 * @brief Opens the Settings modal on top of the current screen.
	 * Pushed as its own history entry, so a back gesture closes it instead of
	 * leaving the screen underneath it — same pattern as gotoAuth.
	 */
	openSettings(): void {
		this.isSettingsOpen = true;
		window.history.pushState(this.#historyState, "", pathForScreen(this.current));
	}

	/**
	 * @brief Closes the Settings modal, leaving the current screen untouched.
	 * Replaces (rather than pushes) the history entry openSettings pushed.
	 */
	closeSettings(): void {
		if (!this.isSettingsOpen) return;
		this.isSettingsOpen = false;
		window.history.replaceState(this.#historyState, "", pathForScreen(this.current));
	}

	/**
	 * @brief Opens the Verification modal on top of the current screen.
	 * Pushed as its own `window.history` entry, so a back gesture closes the
	 * modal instead of leaving the screen underneath it.
	 */
	openVerifyModal(): void {
		this.isVerifyModalOpen = true;
		if (typeof window !== "undefined") {
			window.history.pushState(this.#historyState, "", pathForScreen(this.current));
		}
	}

	/**
	 * @brief Closes the Verification modal, leaving the current screen untouched.
	 * Replaces (rather than pushes) the `window.history` entry openVerifyModal
	 * pushed to open it, so a later back gesture returns to whatever was
	 * current before the modal opened instead of re-opening it.
	 */
	closeVerifyModal(): void {
		if (!this.isVerifyModalOpen) return;
		this.isVerifyModalOpen = false;
		if (typeof window !== "undefined") {
			window.history.replaceState(this.#historyState, "", pathForScreen(this.current));
		}
	}
}

export const storeNavigation = new StoreNavigation();
