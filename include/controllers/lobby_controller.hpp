#pragma once
#include <game/match_instance.hpp>
#include <common/lobby.hpp>
#include <common/ws.hpp>
#include <atomic>
#include <string>
#include <unordered_map>
#include <App.h>
#include <action_router.hpp>
#include <websocket_context.hpp>
#include <webserver.hpp>

/**
 * @file lobby_controller.hpp
 * @brief Definizione del controller per la gestione delle Lobby. Gestisce l'intero I/O tra
 * Client e Server per quanto riguarda l'inizializzazione di Lobby, e l'inizio di Partite di Gioco,
 * a cui successivamente passa il controllo.
 */

/**
 * @typedef GameStartedCallback
 * @brief Callback invocata all'avvio di una partita.
 * @tag LOBBY-TYP-001
 */
using GameStartedCallback = std::function<void(Lobby*)>;

/**
 * @typedef PlayerReplacedCallback
 * @brief Callback invocata quando un giocatore viene sostituito (es. da un bot).
 * @tag LOBBY-TYP-002
 */
using PlayerReplacedCallback = std::function<void(Lobby*)>;

/**
 * @class LobbyController
 * @brief Gestisce lo stato in memoria di tutte le lobby attive.
 * * Questa classe non utilizza un database: le lobby sono effimere e vivono solo
 * per la durata della sessione del server. Gestisce la creazione, l'unione, 
 * le disconnessioni temporanee e la riconnessione dei client tramite WebSocket.
 * * @tag LOBBY-CTRL-000
 */
class LobbyController {
public:
    /**
     * @brief Costruttore del LobbyController.
     * Registra gli handler delle azioni WebSocket sul router fornito.
     * Mantiene un riferimento a `app` per i broadcast pub/sub e il timer di sfratto.
     * * @param webserver Riferimento al WebServer principale.
     * @tag LOBBY-CTRL-001
     */
    LobbyController(WebServer& webserver);

    /**
     * @brief Distruttore. Si occupa di ripulire i timer libuv associati.
     * @tag LOBBY-CTRL-002
     */
    ~LobbyController();

    /**
     * @brief Handler chiamato quando un client stabilisce una nuova connessione WebSocket.
     * Se l'utente era già in una lobby (scenario di riconnessione), aggiorna il puntatore
     * del socket e ripristina la sua presenza.
     * * @param ws Puntatore al socket WebSocket.
     * @param sd Dati associati al socket (contiene username, ecc.).
     * @tag LOBBY-CTRL-003
     */
    void OnOpen(AppWebSocket* ws, PerSocketData* sd);

    /**
     * @brief Handler chiamato quando un client chiude la connessione.
     * Segna il membro come disconnesso e registra il timestamp. Non rimuove immediatamente
     * l'utente, ma avvia un periodo di tolleranza (grace period).
     * * @param ws Puntatore al socket WebSocket disconnesso.
     * @param sd Dati associati al socket.
     * @tag LOBBY-CTRL-004
     */
    void OnClose(AppWebSocket* ws, PerSocketData* sd);

    /**
     * @brief Recupera un puntatore a una lobby tramite il suo codice di invito.
     * * @param code Codice alfanumerico di 6 caratteri (es. "XK4F9Z").
     * @return Lobby* Puntatore alla lobby, o nullptr se non trovata.
     * @tag LOBBY-CTRL-005
     */
    Lobby* GetLobbyByCode(const std::string& code) {
        auto it = code_to_id_.find(code);
        if (it != code_to_id_.end()) {
            return &lobbies_.at(it->second);
        }
        return nullptr;
    }

    /**
     * @brief Recupera un puntatore a una lobby tramite il suo ID numerico interno.
     * * @param id ID univoco generato dal server.
     * @return Lobby* Puntatore alla lobby, o nullptr se non trovata.
     * @tag LOBBY-CTRL-006
     */
    Lobby* GetLobbyById(const uint32_t id) {
        auto it = lobbies_.find(id);
        if (it != lobbies_.end()) {
            return &it->second;
        }
        return nullptr;
    }

    /**
     * @brief Registra una callback da eseguire all'inizio di una partita.
     * @param callback La funzione da invocare.
     * @tag LOBBY-CTRL-007
     */
    void OnGameStarted(GameStartedCallback callback) {
        game_started_callback_ = std::move(callback);
    }

    /**
     * @brief Registra una callback da eseguire quando un giocatore viene sostituito.
     * @param callback La funzione da invocare.
     * @tag LOBBY-CTRL-008
     */
    void OnPlayerReplaced(PlayerReplacedCallback callback) {
        player_replaced_callback_ = std::move(callback);
    }

    /**
     * @brief Salva lo stato corrente della partita associata alla lobby nel database.
     * @param lobby Riferimento alla lobby di cui salvare lo stato.
     * @tag LOBBY-CTRL-009
     */
    void SaveMatchStateToDB(Lobby& lobby);

    /**
     * @brief Tempo di tolleranza (grace period) prima che un utente disconnesso venga espulso.
     * @tag LOBBY-CFG-001
     */
    static constexpr int kReconnectGraceMs = 1'000 * 30;

    /**
     * @brief Numero massimo di giocatori consentiti in una singola lobby.
     * @tag LOBBY-CFG-002
     */
    static constexpr int kMaxMembers = 4;

private:
    /**
     * @brief Sincronizza lo stato dei bot all'interno della lobby.
     * @param lobby La lobby da aggiornare.
     * @tag LOBBY-PRIV-001
     */
    void SyncBots(Lobby& lobby);

    /**
     * @brief Verifica l'integrità dei dati della partita corrente (es. riallineamento host).
     * @param lobby La lobby da verificare.
     * @tag LOBBY-PRIV-002
     */
    void CheckMatchIntegrity(Lobby& lobby);

    /**
     * @brief Gestisce la richiesta di creazione di una nuova lobby.
     * @param ctx Contesto WebSocket della richiesta.
     * @param msg Payload JSON associato.
     * @tag LOBBY-ACT-001
     */
    void HandleCreate(WsContext ctx, const json& msg);

    /**
     * @brief Gestisce la richiesta di unione a una lobby tramite codice.
     * @param ctx Contesto WebSocket della richiesta.
     * @param msg Payload JSON associato.
     * @tag LOBBY-ACT-002
     */
    void HandleJoin(WsContext ctx, const json& msg);

    /**
     * @brief Gestisce la richiesta di riconnessione a una lobby.
     * @param ctx Contesto WebSocket della richiesta.
     * @param msg Payload JSON associato.
     * @tag LOBBY-ACT-003
     */
    void HandleRejoin(WsContext ctx, const json& msg);

    /**
     * @brief Gestisce l'uscita volontaria di un utente dalla lobby.
     * @param ctx Contesto WebSocket della richiesta.
     * @param msg Payload JSON associato.
     * @tag LOBBY-ACT-004
     */
    void HandleLeave(WsContext ctx, const json& msg);

    /**
     * @brief Gestisce la richiesta della lista di lobby pubbliche attive.
     * @param ctx Contesto WebSocket della richiesta.
     * @param msg Payload JSON associato.
     * @tag LOBBY-ACT-005
     */
    void HandleList(WsContext ctx, const json& msg);

    /**
     * @brief Promuove un giocatore specifico al ruolo di Host della lobby.
     * @param ctx Contesto WebSocket della richiesta.
     * @param msg Payload JSON associato.
     * @tag LOBBY-ACT-006
     */
    void HandlePromote(WsContext ctx, const json& msg);

    /**
     * @brief Espelle un giocatore dalla lobby (eseguibile solo dall'Host).
     * @param ctx Contesto WebSocket della richiesta.
     * @param msg Payload JSON associato.
     * @tag LOBBY-ACT-007
     */
    void HandleKick(WsContext ctx, const json& msg);

    /**
     * @brief Aggiorna le impostazioni della lobby (eseguibile solo dall'Host).
     * @param ctx Contesto WebSocket della richiesta.
     * @param msg Payload JSON associato.
     * @tag LOBBY-ACT-008
     */
    void HandleUpdateSettings(WsContext ctx, const json& msg);

    /**
     * @brief Richiede la lista delle partite salvate precedentemente.
     * @param ctx Contesto WebSocket della richiesta.
     * @param msg Payload JSON associato.
     * @tag LOBBY-ACT-009
     */
    void HandleGetSavedMatchesList(WsContext ctx, const json& msg);

    /**
     * @brief Riprende una partita salvata a partire dal suo ID.
     * @param ctx Contesto WebSocket della richiesta.
     * @param msg Payload JSON associato.
     * @tag LOBBY-ACT-010
     */
    void HandleResumeSavedMatch(WsContext ctx, const json& msg);

    /**
     * @brief Elimina in modo permanente una partita salvata dal DB.
     * @param ctx Contesto WebSocket della richiesta.
     * @param msg Payload JSON associato.
     * @tag LOBBY-ACT-011
     */
    void HandleDeleteSavedMatch(WsContext ctx, const json& msg);

    /**
     * @brief Avvia la partita per la lobby corrente. Inizializza l'istanza di gioco.
     * @param context Contesto WebSocket della richiesta.
     * @param message Payload JSON associato.
     * @tag LOBBY-ACT-012
     */
    void HandleStartGame(WsContext context, const nlohmann::json& message);

    /**
     * @brief Genera un codice alfanumerico univoco di 6 caratteri (Invite Code).
     * @return std::string Il codice di invito generato.
     * @tag LOBBY-UTIL-001
     */
    static std::string GenerateInviteCode();

    /**
     * @brief Serializza la lista dei membri in formato JSON per l'invio via WebSocket.
     * @param lobby La lobby di cui serializzare i membri.
     * @return json Oggetto JSON rappresentante i membri.
     * @tag LOBBY-UTIL-002
     */
    static json MemberListJson(const Lobby& lobby);

    /**
     * @brief Invia in broadcast l'evento `lobby_updated` a tutti i membri connessi.
     * @param lobby La lobby da cui inviare il broadcast.
     * @tag LOBBY-UTIL-003
     */
    void BroadcastUpdate(const Lobby& lobby) const;

    /**
     * @brief Rimuove un membro dalla lobby. Distrugge la lobby se risulta vuota.
     * @param lobby_id ID della lobby.
     * @param username Username del giocatore da rimuovere.
     * @param explicit_leave True se l'utente ha abbandonato volontariamente.
     * @param request_id ID della richiesta (opzionale, per tracciamento).
     * @return true se la lobby esiste ancora, false se è stata distrutta.
     * @tag LOBBY-PRIV-003
     */
    bool RemoveMember(uint32_t lobby_id, const string& username, bool explicit_leave = true, const string& request_id = "");

    /**
     * @brief Trova a quale lobby appartiene attualmente un dato utente.
     * @param username L'username da cercare.
     * @return Lobby* Puntatore alla lobby, nullptr se non si trova in nessuna lobby.
     * @tag LOBBY-PRIV-004
     */
    Lobby* FindLobbyForUser(const std::string& username);

    GameStartedCallback game_started_callback_;     /**< Callback per l'avvio partita. */
    PlayerReplacedCallback player_replaced_callback_;/**< Callback per sostituzione giocatori. */

    ActionRouter& action_router_;                   /**< Riferimento al router di azioni WS. */
    uWS::SSLApp& app_;                              /**< Riferimento all'istanza uWebSockets. */

    std::unordered_map<uint32_t, Lobby> lobbies_;   /**< Memorizzazione primaria delle lobby. */
    std::unordered_map<std::string, uint32_t> code_to_id_; /**< Indice secondario per ricerca rapida. */

    std::atomic<uint32_t> next_id_{1};              /**< Contatore thread-safe per gli ID delle lobby. */
    us_timer_t* eviction_timer_ = nullptr;          /**< Timer libuv per l'eviction dei disconnessi. */
};
