/**
 * @file toast.svelte.ts
 * @brief Store globale per la gestione delle notifiche a comparsa (Toast).
 * Fornisce una coda tipizzata e auto-scadente per inviare feedback visivi all'utente
 * da qualsiasi parte dell'applicazione (es. errori di rete o conferme di salvataggio).
 */

/**
 * @typedef ToastType
 * @brief Tipologia della notifica, utilizzata per determinare lo stile e l'icona del toast.
 */
export type ToastType = "success" | "error" | "info" | "warning";

/**
 * @interface Toast
 * @brief Rappresenta una singola notifica attualmente visibile a schermo.
 */
export interface Toast {
    /** Identificativo numerico univoco della notifica, usato per la rimozione. */
    id: number;
    /** Tipo di notifica (determina il colore di sfondo/bordo). */
    type: ToastType;
    /** Il messaggio testuale da mostrare all'utente. */
    message: string;
}

/**
 * @class StoreToast
 * @brief Gestisce la coda reattiva delle notifiche e il loro ciclo di vita temporale.
 * @tag FRONT-TOAST-001
 */
class StoreToast {
    /** Array reattivo contenente le notifiche attualmente attive e visibili. */
    items = $state<Toast[]>([]);

    /** Contatore incrementale interno per assegnare un ID univoco ad ogni nuova notifica. */
    #nextId = 0;

    /**
     * @brief Aggiunge una nuova notifica alla coda e imposta un timer per la sua rimozione.
     * @param message Il testo della notifica.
     * @param type La gravità/categoria della notifica (Default: "info").
     * @param durationMs Millisecondi prima della scomparsa automatica (Default: 5000ms). Se 0, il toast non scade.
     * @returns Una funzione di callback che, se invocata, rimuove prematuramente la notifica.
     * @tag FRONT-TOAST-MTH-001
     */
    add(message: string, type: ToastType = "info", durationMs = 5_000): () => void {
        const id = this.#nextId++;
        this.items.push({ id, type, message });

        const remove = () => this.remove(id);
        if (durationMs > 0) setTimeout(remove, durationMs);
        return remove;
    }

    /**
     * @brief Rimuove immediatamente una notifica dall'array filtrandola per ID.
     * L'interfaccia utente Svelte aggiornerà la lista eliminando il componente dal DOM.
     * @param id L'identificativo numerico della notifica da eliminare.
     * @tag FRONT-TOAST-MTH-002
     */
    remove(id: number): void {
        this.items = this.items.filter((t) => t.id !== id);
    }

    // --- Metodi di utilità rapida (Shorthands) ---

    /**
     * @brief Scorciatoia per mostrare un messaggio di errore (Resta visibile per 6 secondi).
     * @param msg Il messaggio di errore.
     */
    error(msg: string): void {
        this.add(msg, "error", 6_000);
    }

    /**
     * @brief Scorciatoia per mostrare un messaggio di successo (Resta visibile per 3 secondi).
     * @param msg Il messaggio di successo.
     */
    success(msg: string): void {
        this.add(msg, "success", 3_000);
    }

    /**
     * @brief Scorciatoia per mostrare un messaggio informativo (Resta visibile per 5 secondi).
     * @param msg Il messaggio informativo.
     */
    info(msg: string): void {
        this.add(msg, "info", 5_000);
    }

    /**
     * @brief Scorciatoia per mostrare un avvertimento (Resta visibile per 5 secondi).
     * @param msg Il messaggio di avvertimento.
     */
    warning(msg: string): void {
        this.add(msg, "warning", 5_000);
    }
}

/**
 * @brief Istanza singleton esportata globalmente. È il punto di accesso per tutti gli altri store.
 */
export const storeToast = new StoreToast();
