/**
 * storeNavigation — screen routing.
 *
 * Keeps one level of history for a simple "back" action.
 */

export type AppScreen = "main" | "auth" | "lobbies" | "lobby" | "game" | "stats";

class StoreNavigation {
    // ── Reactive state ──────────────────────────────────────────────────────
    current = $state<AppScreen>("main");
    #previous: AppScreen | null = null;

    // ── Public API ───────────────────────────────────────────────────────────

    goto(screen: AppScreen): void {
        if (screen === this.current) return;
        this.#previous = this.current;
        this.current = screen;
    }

    back(): void {
        if (this.#previous) this.goto(this.#previous);
    }
}

export const storeNavigation = new StoreNavigation();
