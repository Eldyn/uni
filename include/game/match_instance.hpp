#pragma once

#include "common/lobby.hpp"
#include <common/game/gamerule.hpp>
#include <controllers/lobby_controller.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <memory>
#include <algorithm>

/**
 * @file match_instance.hpp
 * @brief Definizione della classe principale che gestisce l'intero ciclo di vita di una partita di gioco:
 * una state machine che applica gli effetti di ogni carta giocata in base a delle regole che ne definiscono 
 * le varie propietà.
 */

struct LobbySettings;

namespace game {
    
    /**
     * @class MatchInstance
     * @brief Rappresenta un'istanza attiva di una partita (Match).
     * * Mantiene lo stato di gioco interno (`GameState`), gestisce il flusso dei turni,
     * l'esecuzione delle mosse (giocare carte, pescare), e processa gli effetti e le
     * regole attive applicate a questa partita specifica.
     * * @tag MATCH-INST-000
     */
    class MatchInstance {
    public:
        /**
         * @brief Costruttore per una nuova partita partendo dalle informazioni dei giocatori e impostazioni.
         * @param players_info Vettore di coppie {username, is_bot} indicanti i partecipanti.
         * @param settings Impostazioni correnti della lobby.
         * @tag MATCH-INST-001
         */
        explicit MatchInstance(const std::vector<std::pair<std::string, bool>>& players_info, const LobbySettings& settings);
        
        /**
         * @brief Costruttore per ricaricare una partita partendo da uno stato salvato nel database.
         * @param saved_state Il JSON contenente lo stato serializzato.
         * @param settings Impostazioni della lobby associate al salvataggio.
         * @tag MATCH-INST-002
         */
        explicit MatchInstance(const json& saved_state, const LobbySettings& settings);
        
        /**
         * @brief Inizializza il match, crea il mazzo iniziale e stabilisce i turni.
         * @tag MATCH-INST-003
         */
        void Start();

        /**
         * @brief Aggiornamento periodico della partita (utilizzato per elaborare gli effetti in coda o timeout).
         * @tag MATCH-INST-004
         */
        void Tick(); 
        
        /**
         * @brief Richiesta da parte di un utente di giocare una determinata carta.
         * @param username L'utente che sta giocando la carta.
         * @param card_id L'identificativo a 16-bit della carta compatta.
         * @return true se la giocata è stata validata ed elaborata, false altrimenti.
         * @tag MATCH-INST-005
         */
        bool PlayCard(const std::string& username, uint16_t card_id);

        /**
         * @brief Richiesta da parte di un utente di pescare una carta dal mazzo.
         * @param username L'utente che intende pescare.
         * @return true se l'azione è permessa ed eseguita, false altrimenti.
         * @tag MATCH-INST-006
         */
        bool DrawCard(const std::string& username);

        /**
         * @brief Fornisce un input ad un effetto in sospeso (es. Scelta di un colore).
         * @param username Il giocatore che fornisce l'input.
         * @param input Stringa rappresentante la scelta effettuata.
         * @tag MATCH-INST-007
         */
        void ProvideInput(const std::string& username, const std::string& input);

        /**
         * @brief Dichiara "UNO" per lo specifico giocatore.
         * @param username Il giocatore che sta chiamando UNO.
         * @tag MATCH-INST-008
         */
        void CallUno(const std::string& username);
    
        /**
         * @brief Richiama l'intelligenza artificiale per far eseguire il turno al Bot.
         * @tag MATCH-INST-009
         */
        void TakeBotTurn();

        /**
         * @brief Restituisce il nome utente del giocatore il cui turno è attualmente attivo.
         * @return std::string Nome del giocatore.
         * @tag MATCH-INST-010
         */
        std::string GetCurrentPlayerUsername() const;

        /**
         * @brief Aggiunge o sostituisce un giocatore a partita in corso (es. Takeover bot).
         * @param username Il nuovo giocatore.
         * @param is_bot True se si tratta di un bot, False per un umano.
         * @tag MATCH-INST-011
         */
        void AddPlayerMidGame(const std::string& username, bool is_bot);

        /**
         * @brief Rimuove un giocatore a partita in corso, trasformandolo tipicamente in Bot.
         * @param username Il giocatore disconnesso/uscito.
         * @tag MATCH-INST-012
         */
        void RemovePlayerMidGame(const std::string& username);
    
        /**
         * @brief Esporta l'intero stato del GameState in un formato JSON salvabile.
         * @return json Stato serializzato.
         * @tag MATCH-INST-013
         */
        json ExportState() const;

        /**
         * @brief Verifica se il motore di gioco è in attesa di un input esplicito da un giocatore.
         * @return true se l'effetto corrente ha bisogno di input.
         * @tag MATCH-INST-014
         */
        bool IsWaitingForInput() const { return !state_.pending_player.empty(); }

        /**
         * @brief Imposta il limite di tempo per la fine del turno corrente.
         * @param end_time Il timestamp in cui il turno scadrà (triggerando l'AFK o bot).
         * @tag MATCH-INST-015
         */
        void SetTurnEndTime(std::chrono::steady_clock::time_point end_time) { state_.turn_end_time = end_time; }

        /**
         * @brief Recupera i dati interni di un giocatore specifico tramite username.
         * @param username Identificativo dell'utente.
         * @return Player* Puntatore ai dati del giocatore o nullptr se non esiste.
         * @tag MATCH-INST-016
         */
        Player* GetPlayer(const std::string& username);

        /**
         * @brief Restituisce l'utente dal quale si sta attendendo un input obbligatorio.
         * @return std::string Username o stringa vuota se non c'è attesa.
         * @tag MATCH-INST-017
         */
        std::string GetPendingPlayer() const { return state_.pending_player; }

        /**
         * @brief Restituisce il tipo di input in sospeso (es. "choose_color").
         * @return std::string Codice dell'input.
         * @tag MATCH-INST-018
         */
        std::string GetPendingInputType() const { return state_.pending_input_type; }

        /**
         * @brief Restituisce dati testuali JSON contestuali alla richiesta di input.
         * @return std::string Il contesto dell'input.
         * @tag MATCH-INST-019
         */
        std::string GetPendingInputContext() const { return state_.pending_input_context; }
        
        /**
         * @brief Determina se la partita ha raggiunto uno stato terminale.
         * @return true se terminata.
         * @tag MATCH-INST-020
         */
        bool IsGameOver() const { return state_.status == MatchStatus::kFinished; }

        /**
         * @brief Recupera l'username del vincitore (se la partita è conclusa).
         * @return std::string Username del vincitore.
         * @tag MATCH-INST-021
         */
        std::string GetWinner() const { return state_.winner; }
    
        /**
         * @brief Crea un JSON che rappresenta lo stato di gioco mascherato,
         * specifico per il punto di vista del giocatore fornito (nascondendo mani avversarie).
         * * @param username Il giocatore per il quale si genera la vista.
         * @return nlohmann::json Lo stato censurato e pronto all'invio al frontend.
         * @tag MATCH-INST-022
         */
        nlohmann::json SerializePlayerState(const std::string& username) const;

        /**
         * @brief Recupera l'UUID univoco della partita (per salvataggio su DB).
         * @return std::string ID della partita.
         * @tag MATCH-INST-023
         */
        std::string GetMatchId() const { return match_id_; }

        /**
         * @brief Assegna un UUID persistente alla partita.
         * @param id L'identificativo da assegnare.
         * @tag MATCH-INST-024
         */
        void SetMatchId(const std::string& id) { match_id_ = id; }

    private:
        GameState state_;                        /**< Lo stato di gioco centrale. */
        LobbySettings settings_;                 /**< Le regole e preferenze della partita. */
        std::string match_id_;                   /**< Identificatore univoco del match sul database. */

        std::unordered_map<std::string, PlayerSessionStats> session_stats_; /**< Statistiche raccolte durante il match. */
        std::vector<std::unique_ptr<GameRule>> active_rules_;              /**< Set di regole attive. */
    
        /**
         * @brief Checks whether a given optional rule mod is currently active for this match.
         * @param name The mod identifier (e.g. "seven_zero", "draw_stacking", "no_bluffing").
         * @return true if the mod is enabled in this lobby's settings.
         * @tag MATCH-INST-PRIV-001
         */
        bool HasMod(const std::string& name) const {
            return std::find(settings_.active_mods.begin(), settings_.active_mods.end(), name)
                   != settings_.active_mods.end();
        }

        /**
         * @brief Inizializza il mazzo di carte basandosi sulle LobbySettings fornite.
         * @tag MATCH-PRIV-002
         */
        void GenerateDeck();
    };
}

