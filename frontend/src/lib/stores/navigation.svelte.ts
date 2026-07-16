/**
 * @file navigation.svelte.ts
 * @brief Manager of the application's internal routing (Single Page Application).
 * Keeps a history limited to one level in memory to handle the "Back" action.
 */

import { storeAnalytics } from "./analytics.svelte";
import { storeAuth } from "./auth.svelte";
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
 * @class StoreNavigation
 * @brief Reactive store for screen switching.
 * Uses localStorage to persist the current screen and restore it
 * after a reload (F5), after verifying the login state.
 * @tag FRONT-NAV-001
 */
class StoreNavigation {
	/** The screen currently displayed to the user. */
	current = $state<AppScreen>("main");

	/** Whether the AuthScreen modal is open, overlaid on top of whatever screen is current. */
	isAuthModalOpen = $state(false);

	/** Which tab the AuthScreen modal should open on. */
	authTab = $state<"login" | "register">("login");

	/** Stores the previous screen for 'back' navigation. */
	#previous: AppScreen | null = null;

	#screenRestored = false;

	/** Coarse, non-identifying account bucket for analytics segmentation. */
	get #accountType(): "registered" | "guest" | "anonymous" {
		if (storeAuth.isLoggedIn) return "registered";
		if (storeAuth.isGuest) return "guest";
		return "anonymous";
	}

	constructor() {
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

	/**
	 * @brief Changes the current screen.
	 * Automatically saves the screen to `localStorage`.
	 * @param screen The new destination screen.
	 */
	goto(screen: AppScreen): void {
		if (screen === this.current) return;
		this.#previous = this.current;
		this.current = screen;
		localStorage.setItem("currentScreen", screen);
		storeAnalytics.track("screen_view", { screen, account_type: this.#accountType });
	}

	/**
	 * @brief Opens the auth modal on top of the current screen.
	 * @param tab Tab the modal should start on. Defaults to "login".
	 */
	gotoAuth(tab: "login" | "register" = "login"): void {
		this.authTab = tab;
		this.isAuthModalOpen = true;
	}

	/**
	 * @brief Closes the auth modal, leaving the current screen untouched.
	 */
	closeAuthModal(): void {
		this.isAuthModalOpen = false;
	}

	/**
	 * @brief Returns to the previous screen, if available in memory.
	 */
	back(): void {
		if (this.#previous) this.goto(this.#previous);
	}
}

export const storeNavigation = new StoreNavigation();
