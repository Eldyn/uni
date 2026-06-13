/**
 * storeNavigation — screen routing.
 *
 * Keeps one level of history for a simple "back" action.
 */

import { storeAuth } from "./auth.svelte";
import { ws } from "./ws.svelte";

export type AppScreen =
    | "main"
    | "auth"
    | "lobbies"
    | "lobby"
    | "game"
    | "settings"
    | "stats"
    | "detailedStats";

class StoreNavigation {
    current = $state<AppScreen>("main");
    #previous: AppScreen | null = null;

    constructor() {
        ws.onOpen(() => {
            const localScreen = localStorage.getItem("currentScreen");
            if (!localScreen) return;
            if (!storeAuth.isLoggedIn) {
                localStorage.removeItem("currentScreen");
                return;
            }

            this.goto(localScreen as AppScreen);
        });
    }

    goto(screen: AppScreen): void {
        if (screen === this.current) return;
        this.#previous = this.current;
        this.current = screen;
        localStorage.setItem("currentScreen", screen);
    }

    back(): void {
        if (this.#previous) this.goto(this.#previous);
    }
}

export const storeNavigation = new StoreNavigation();
