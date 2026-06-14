/**
 * @file stats.svelte.ts
 * @brief Store globale reattivo per la gestione delle statistiche e della classifica globale.
 * Si interfaccia con il backend REST per recuperare i dati dell'utente e la Top 50.
 */

import { storeToast } from "./toast.svelte";

/**
 * @interface PlayerStats
 * @brief Rappresenta le statistiche complete di un singolo giocatore restituite dal server.
 * Include vittorie, sconfitte e un dettaglio analitico delle singole carte giocate (se disponibile).
 */
export interface PlayerStats {
    /** L'username univoco del giocatore. */
    username: string;
    /** Numero totale di partite vinte. */
    total_wins: number;
    /** Numero totale di partite perse. */
    total_losses: number;
    /** Posizione nella classifica globale (null se il giocatore non è ancora classificato). */
    rank: number | null;

    /** Contatore di carte rosse giocate (Opzionale). */
    cards_played_red?: number;
    /** Contatore di carte blu giocate (Opzionale). */
    cards_played_blue?: number;
    /** Contatore di carte verdi giocate (Opzionale). */
    cards_played_green?: number;
    /** Contatore di carte gialle giocate (Opzionale). */
    cards_played_yellow?: number;
    /** Contatore di carte '0' giocate (Opzionale). */
    cards_played_0?: number;
    /** Contatore di carte '1' giocate (Opzionale). */
    cards_played_1?: number;
    /** Contatore di carte '2' giocate (Opzionale). */
    cards_played_2?: number;
    /** Contatore di carte '3' giocate (Opzionale). */
    cards_played_3?: number;
    /** Contatore di carte '4' giocate (Opzionale). */
    cards_played_4?: number;
    /** Contatore di carte '5' giocate (Opzionale). */
    cards_played_5?: number;
    /** Contatore di carte '6' giocate (Opzionale). */
    cards_played_6?: number;
    /** Contatore di carte '7' giocate (Opzionale). */
    cards_played_7?: number;
    /** Contatore di carte '8' giocate (Opzionale). */
    cards_played_8?: number;
    /** Contatore di carte '9' giocate (Opzionale). */
    cards_played_9?: number;
    /** Contatore di carte 'Divieto/Salta' giocate (Opzionale). */
    cards_played_skip?: number;
    /** Contatore di carte 'Inverti' giocate (Opzionale). */
    cards_played_reverse?: number;
    /** Contatore di carte '+2' giocate (Opzionale). */
    cards_played_draw2?: number;
    /** Contatore di carte '+4' giocate (Opzionale). */
    cards_played_draw4?: number;
    /** Contatore di carte 'Cambio Colore' giocate (Opzionale). */
    cards_played_colorswitch?: number;
}

/**
 * @class StoreStats
 * @brief Incapsula lo stato e i metodi per recuperare le statistiche dal server.
 * @tag FRONT-STAT-001
 */
class StoreStats {
    /** Stato reattivo contenente le statistiche personali dell'utente loggato. */
    myStats = $state<PlayerStats | null>(null);

    /** Stato reattivo contenente la classifica dei migliori 50 giocatori. */
    leaderboard = $state<PlayerStats[]>([]);

    /** Flag booleano che indica se è in corso una richiesta di rete, utile per gli spinner UI. */
    isLoading = $state(false);

    /**
     * @brief Recupera le statistiche personali dal server.
     * Effettua una richiesta GET a `/stats/me` includendo le credenziali (cookie).
     * In caso di successo, decodifica il JSON e popola lo stato `myStats`.
     * @returns Promise vuota risolta al termine dell'operazione di rete.
     * @tag FRONT-STAT-MTH-001
     */
    async fetchMe(): Promise<void> {
        try {
            const res = await fetch("/stats/me", {
                credentials: "include",
                headers: { "Content-Type": "application/json" }
            });

            if (res.ok) {
                this.myStats = await res.json();
            }
        } catch {
            storeToast.error("Failed to load personal statistics.");
        }
    }

    /**
     * @brief Recupera la classifica globale dal server.
     * Effettua una richiesta GET a `/stats/leaderboard` e aggiorna l'array di stato `leaderboard`
     * per permettere alla UI di renderizzare la tabella. Gestisce in automatico il loading state.
     * @returns Promise vuota risolta al termine dell'operazione.
     * @tag FRONT-STAT-MTH-002
     */
    async fetchLeaderboard(): Promise<void> {
        this.isLoading = true;
        try {
            const res = await fetch("/stats/leaderboard", {
                credentials: "include",
                headers: { "Content-Type": "application/json" }
            });

            if (res.ok) {
                const data = await res.json();
                this.leaderboard = data.leaderboard || [];
            } else {
                storeToast.error("Failed to load leaderboard.");
            }
        } catch {
            storeToast.error("Network error while loading leaderboard.");
        } finally {
            this.isLoading = false;
        }
    }
}

/**
 * @brief Istanza singleton esportata per essere utilizzata dai componenti Svelte.
 */
export const storeStats = new StoreStats();
