/**
 * @file navigation.svelte.ts
 * @brief Gestore del routing interno dell'applicazione (Single Page Application).
 * Mantiene in memoria lo storico limitato a un livello per gestire l'azione "Indietro".
 */

import { storeAuth } from "./auth.svelte";
import { ws } from "./ws.svelte";

/**
 * @typedef AppScreen
 * @brief Elenco delle schermate disponibili nell'applicazione frontend.
 */
export type AppScreen =
    | "main"
    | "auth"
    | "lobbies"
    | "lobby"
    | "game"
    | "settings"
    | "stats"
    | "detailedStats";

/**
 * @class StoreNavigation
 * @brief Store reattivo per il cambio di schermata.
 * Sfrutta il localStorage per persistere la schermata corrente e ripristinarla
 * dopo un ricaricamento (F5), previa verifica dello stato di login.
 * @tag FRONT-NAV-001
 */
class StoreNavigation {
    /** La schermata attualmente visualizzata dall'utente. */
    current = $state<AppScreen>("main");

    /** Memorizza la schermata precedente per la navigazione 'back'. */
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

    /**
     * @brief Cambia la schermata corrente.
     * Salva automaticamente la schermata nel `localStorage`.
     * @param screen La nuova schermata di destinazione.
     */
    goto(screen: AppScreen): void {
        if (screen === this.current) return;
        this.#previous = this.current;
        this.current = screen;
        localStorage.setItem("currentScreen", screen);
    }

    /**
     * @brief Ritorna alla schermata precedente, se disponibile in memoria.
     */
    back(): void {
        if (this.#previous) this.goto(this.#previous);
    }
}

export const storeNavigation = new StoreNavigation();
