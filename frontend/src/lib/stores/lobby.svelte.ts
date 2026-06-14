/**
 * @file lobby.svelte.ts
 * @brief Store globale reattivo per la gestione delle lobby e delle partite salvate.
 * Gestisce la creazione, l'unione, l'aggiornamento delle impostazioni e la lista pubblica.
 */

import { storeNavigation } from "./navigation.svelte";
import { storeToast } from "./toast.svelte";
import { ClientAction, ServerAction, ws } from "./ws.svelte";

/**
 * @enum BotTakeoverMode
 * @brief Modalità di comportamento quando un bot sostituisce o viene sostituito.
 */
export enum BotTakeoverMode {
    /** Il bot gioca istantaneamente la sua mossa non appena arriva il suo turno. */
    PlayInstantly,
    /** Il bot attende fino a 5 secondi per il suo turno, o fino alla fine del turno per altri giocatori AFK */
    WaitUntilTurnEnd
}

/**
 * @interface SavedMatch
 * @brief Dati di base di una partita salvata sul server.
 */
export interface SavedMatch {
    /** Identificativo univoco del salvataggio nel database. */
    match_id: string;
    /** Data e ora del salvataggio. */
    saved_at: string;
    /** Lista degli username dei giocatori presenti in quella partita. */
    players: string[];
}

/**
 * @interface LobbySettings
 * @brief Rappresenta la configurazione e le regole scelte per la lobby.
 * Definisce i parametri della partita e la composizione del mazzo iniziale.
 */
export interface LobbySettings {
    /** Se true, la lobby apparirà nella lista pubblica delle partite. */
    is_public: boolean;
    /** Array di regole speciali (Mod) attivate per questa partita. */
    active_mods: string[];
    /** Limite di tempo in millisecondi concesso per completare un turno (es. 15000). */
    turn_time_limit_ms: number;

    /** Se true, lo stato della partita verrà salvato nel database ad ogni mossa. */
    save_state: boolean;
    /** Se true, l'abbandono volontario di un giocatore eliminerà definitivamente il salvataggio. */
    quit_deletes_match: boolean;

    /** Numero di carte distribuite a ciascun giocatore all'inizio della partita. */
    starting_cards: number;

    /** Numero di bot da inserire automaticamente all'avvio della partita. */
    bot_count: number;
    /** Comportamento del bot (es. gioca subito o attende il timer). */
    bot_mode: BotTakeoverMode;
    /** Se true, i nuovi giocatori umani possono entrare in partita sostituendo un bot. */
    allow_bot_takeover: boolean;
    /** Se true, un giocatore umano che abbandona viene rimpiazzato da un bot anziché terminare il gioco. */
    allow_bot_replacement: boolean;

    /** Copie per colore del numero 0 nel mazzo. */
    count_zeros: number;
    /** Copie per colore dei numeri da 1 a 9 nel mazzo. */
    count_numbered: number;
    /** Copie per colore della carta "Divieto/Salta Turno". */
    count_skips: number;
    /** Copie per colore della carta "Inverti Giro". */
    count_reverses: number;
    /** Copie per colore della carta "Pesca Due (+2)". */
    count_draw_two: number;
    /** Numero totale di carte Jolly (Cambio Colore) nel mazzo. */
    count_wild: number;
    /** Numero totale di carte Jolly Pesca Quattro (+4) nel mazzo. */
    count_wild_draw_four: number;
}

/**
 * @interface LobbyMember
 * @brief Rappresenta un singolo giocatore connesso all'interno di una lobby.
 */
export interface LobbyMember {
    /** Il nome visualizzato del giocatore. */
    username: string;
    /** Indica se il giocatore è attualmente connesso tramite WebSocket o è in fase di riconnessione. */
    is_connected: boolean;
    /** Vero se questo giocatore è il proprietario/host della lobby e può avviare la partita. */
    is_host: boolean;
    /** Vero se questo "membro" è in realtà un account bot gestito dal server. */
    is_bot: boolean;
}

/**
 * @interface Lobby
 * @brief Rappresenta una lobby di gioco completa con tutti i suoi dati di stato.
 */
export interface Lobby {
    /** Il codice univoco alfanumerico di 6 caratteri utilizzato per unirsi. */
    invite_code: string;
    /** L'username del creatore (Host) della lobby. */
    host: string;
    /** Il nome personalizzato della lobby mostrato nella lista pubblica. */
    name: string;
    /** Il numero attuale di membri presenti nella lobby (massimo 4). */
    member_count: number;
    /** Lista dettagliata dei giocatori attualmente nella lobby. */
    members: LobbyMember[];
    /** Le impostazioni configurate per questa stanza di gioco. */
    settings: LobbySettings;
}

/**
 * @interface ListedLobby
 * @brief Dati minimizzati inviati dal server per renderizzare la lista delle lobby pubbliche.
 * Risparmia banda non inviando la lista completa dei membri e le regole dettagliate.
 */
export interface ListedLobby {
    /** Il nome personalizzato della lobby. */
    name: string;
    /** Numero totale di giocatori umani attualmente connessi. */
    member_count: number;
    /** Numero totale di bot presenti in questa lobby. */
    bot_count: number;
    /** Il codice di invito per permettere al client di unirsi con un click. */
    invite_code: string;
}

/**
 * @class StoreLobby
 * @brief Store Singleton che gestisce tutto lo stato relativo alle Lobby e le interazioni WebSocket associate.
 * @tag FRONT-LOBBY-001
 */
class StoreLobby {
    /** Le partite salvate compatibili con la lista giocatori attuale. Null se non si è in lobby. */
    savedMatches = $state<SavedMatch[] | null>(null);
    /** La lobby in cui l'utente si trova attualmente. Null se non si è in lobby. */
    current = $state<Lobby | null>(null);

    /** La lista delle lobby pubbliche disponibili per unirsi. */
    available = $state<ListedLobby[]>([]);

    /** True durante il recupero della lista delle lobby pubbliche dal server. */
    isLoadingList = $state(false);

    /** True quando è in corso una richiesta di creazione o unione a una lobby. */
    isLoadingJoin = $state(false);

    /** True quando è in corso il recupero della lista delle partite salvate. */
    isLoadingSavedMatchList = $state(false);

    /**
     * @brief Proprietà derivata per controllare rapidamente se l'utente è in una lobby.
     * @returns True se l'utente è in una lobby, false altrimenti.
     */
    get isInLobby(): boolean {
        return this.current !== null;
    }

    /**
     * @brief Inizializza lo store e associa gli eventi di connessione di base.
     * @tag FRONT-LOBBY-MTH-001
     */
    constructor() {
        ws.onOpen(async () => {
            this.#registerListeners();
            await this.#tryRejoin();
        });
    }

    /**
     * @brief Promuove un utente al ruolo di host della lobby.
     * @param username Il nome del giocatore da promuovere.
     * @tag FRONT-LOBBY-MTH-002
     */
    async promote(username: string): Promise<void> {
        await ws.connect();
        ws.emit(ClientAction.LobbyPromote, { username });

        storeToast.success(`Promoted ${username}!`);
    }

    /**
     * @brief Espelle un giocatore dalla lobby.
     * @param username Il nome del giocatore da espellere.
     * @tag FRONT-LOBBY-MTH-003
     */
    async kick(username: string): Promise<void> {
        await ws.connect();
        ws.emit(ClientAction.LobbyKick, { username });

        storeToast.success(`Kicked ${username}!`);
    }

    /**
     * @brief Aggiorna le impostazioni della lobby (inclusi nome e visibilità).
     * @param settings I campi delle impostazioni da modificare.
     * @tag FRONT-LOBBY-MTH-004
     */
    async updateSettings(
        settings: Partial<LobbySettings> & Partial<{ name: string }>
    ): Promise<void> {
        try {
            await ws.connect();
            const response = await ws.emitAndWait(ClientAction.LobbyUpdateSettings, settings);
            if (!response.ok) {
                storeToast.error(response.reason);
                return;
            }

            storeToast.success("Impostazioni Aggiornate!");
        } catch (error) {
            storeToast.error("Failed to update lobby settings.");
        }
    }

    /**
     * @brief Crea una nuova lobby di gioco sul server.
     * Si connette automaticamente al WebSocket ed emette il payload di creazione.
     * @param data La configurazione per la nuova lobby.
     * @returns Una Promise che si risolve al completamento della richiesta di rete.
     * @tag FRONT-LOBBY-MTH-005
     */
    async create(data: { is_public: boolean; name: string }): Promise<void> {
        this.isLoadingJoin = true;

        try {
            await ws.connect();
            const response = await ws.emitAndWait(ClientAction.LobbyCreate, data);

            if (!response.ok) {
                storeToast.error(response.reason);
            }
        } finally {
            this.isLoadingJoin = false;
        }
    }

    /**
     * @brief Si unisce a una lobby esistente utilizzando un codice di invito a 6 caratteri.
     * @param code L'identificatore univoco a 6 caratteri della lobby.
     * @returns Una Promise che si risolve al completamento del tentativo di unione.
     * @tag FRONT-LOBBY-MTH-006
     */
    async join(code: string): Promise<void> {
        if (!code) {
            storeToast.error("Enter an invite code.");
            return;
        }

        this.isLoadingJoin = true;

        try {
            await ws.connect();
            const response = await ws.emitAndWait(ClientAction.LobbyJoin, { code });

            if (!response.ok) {
                storeToast.error(response.reason);
            }
        } finally {
            this.isLoadingJoin = false;
        }
    }

    /**
     * @brief Recupera dal server la lista aggiornata delle lobby pubbliche disponibili.
     * Aggiorna l'array di stato `available`.
     * @returns Una Promise che si risolve quando la lista è stata aggiornata.
     * @tag FRONT-LOBBY-MTH-007
     */
    async fetchList(): Promise<void> {
        this.isLoadingList = true;

        try {
            await ws.connect();
            const response = await ws.emitAndWait(ClientAction.LobbyList);

            if (response.ok) {
                this.available = response.get<ListedLobby[]>("lobbies") ?? [];
            }
        } finally {
            this.isLoadingList = false;
        }
    }

    /**
     * @brief Emette una richiesta per abbandonare la lobby corrente.
     * @tag FRONT-LOBBY-MTH-008
     */
    leave(): void {
        ws.emit(ClientAction.LobbyLeave);
    }

    /**
     * @brief Si iscrive a tutti gli eventi WebSocket inviati dal server relativi alle lobby.
     * Gestisce eventi come l'unione, la ricezione di aggiornamenti o l'espulsione.
     * @tag FRONT-LOBBY-PRIV-001
     */
    #registerListeners(): void {
        ws.on(ServerAction.LobbyJoined, async (data) => {
            const lobby = data.lobby as Lobby;
            this.current = lobby;

            localStorage.setItem("lobby_code", lobby.invite_code);
            storeNavigation.goto("lobby");

            await this.#fetchSavedMatches();
        });

        ws.on(ServerAction.LobbyUpdated, async (data) => {
            const updatedLobby = data.lobby as Lobby;
            if (!updatedLobby) return;

            if (this.current) {
                this.current = updatedLobby;
                console.log(this.current);
            }

            const idx = this.available.findIndex((l) => l.invite_code === updatedLobby.invite_code);
            if (idx !== -1) {
                this.available[idx].member_count = updatedLobby.member_count;
                this.available[idx].bot_count = updatedLobby.settings.bot_count;
                this.available[idx].name = updatedLobby.name;
            }

            await this.#fetchSavedMatches();
        });

        const handleDisconnection = () => {
            this.#reset();
            storeNavigation.goto("lobbies");
        };

        ws.on(ServerAction.LobbyLeft, handleDisconnection);
        ws.on(ServerAction.LobbyEvicted, handleDisconnection);
    }

    /**
     * @brief Recupera dal server i salvataggi pregressi compatibili con i giocatori nella lobby.
     * @tag FRONT-LOBBY-PRIV-002
     */
    async #fetchSavedMatches(): Promise<void> {
        if (!this.current) return;

        this.isLoadingSavedMatchList = true;
        try {
            const result = await ws.emitAndWait(ClientAction.LobbyListSavedMatches);
            if (result.ok) {
                this.savedMatches = result.getOr<SavedMatch[]>("saved_matches", []);
            } else {
                storeToast.error("Saved games list error");
            }
        } catch (err) {
            console.error("Failed to parse saved rooms details:", err);
        } finally {
            this.isLoadingSavedMatchList = false;
        }
    }

    /**
     * @brief Tenta silenziosamente di ricollegarsi a una lobby usando un codice sessione memorizzato.
     * Legge `lobby_code` dal localStorage. Viene innescata automaticamente dopo un ricaricamento o disconnessione.
     * @returns Una Promise che si risolve al completamento del flusso di riconnessione.
     * @tag FRONT-LOBBY-PRIV-003
     */
    async #tryRejoin(): Promise<void> {
        const code = localStorage.getItem("lobby_code");
        if (!code) return;

        try {
            const unsubscribeGameRejoin = ws.on(ServerAction.GameStateUpdated, (_data) => {
                storeNavigation.goto("game");

                unsubscribeGameRejoin();
            });

            const response = await ws.emitAndWait(ClientAction.LobbyRejoin, { code });

            if (!response.ok) {
                this.#reset();
                if (response.action !== ServerAction.LobbyEvicted) {
                    storeToast.error(`Could not rejoin lobby: ${response.reason}`);
                }
                return;
            }

            const lobby = response.get<Lobby>("lobby");
            if (!lobby) throw new Error("Invalid lobby data received.");

            this.current = lobby;
            storeNavigation.goto("lobby");

            await this.#fetchSavedMatches();
        } catch (error) {
            this.#reset();
        }
    }

    /**
     * @brief Ripulisce lo stato attivo della lobby e la memoria locale.
     * @tag FRONT-LOBBY-PRIV-004
     */
    #reset(): void {
        this.current = null;
        localStorage.removeItem("lobby_code");
    }
}

export const storeLobby = new StoreLobby();
