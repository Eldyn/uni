/**
 * @file auth.svelte.ts
 * @brief Store per la gestione dell'autenticazione, sessione e validazione.
 * Concentra tutte le chiamate HTTP RESTful al modulo `/auth` e mappa
 * gli errori del server in messaggi validi per i form dell'interfaccia.
 */

import {
    validateEmail,
    validatePassword,
    validateUsername,
    validatePasswordMatch
} from "../utils/validation";
import { storeNavigation } from "./navigation.svelte";
import { storeToast } from "./toast.svelte";

/**
 * @interface AuthFieldErrors
 * @brief Mappatura degli errori per ogni singolo campo del form di registrazione/login.
 */
export interface AuthFieldErrors {
    username?: string;
    email?: string;
    password?: string;
    confirmPassword?: string;
}

/**
 * @class StoreAuth
 * @brief Gestisce lo stato reattivo dell'utente autenticato (Username, Avatar).
 * @tag FRONT-AUTH-001
 */
class StoreAuth {
    /** Nome utente dell'account correntemente connesso. */
    username = $state("");
    /** Immagine di profilo o URL dell'avatar locale. */
    avatar = $state("");
    /** Flag che indica se l'utente possiede una sessione valida. */
    isLoggedIn = $state(false);
    /** Flag per mostrare i caricamenti (spinners) durante le chiamate HTTP. */
    isLoading = $state(false);

    /**
     * @brief Chiamata all'avvio dell'app per ripristinare una sessione preesistente.
     * Sfrutta l'invio automatico dei cookie HttpOnly verso l'endpoint `/auth/me`.
     * @returns {Promise<boolean>} True se l'utente è loggato, false altrimenti.
     */
    async checkSession(): Promise<boolean> {
        try {
            const res = await fetch("/auth/me", {
                credentials: "include",
                headers: { "Content-Type": "application/json" }
            });

            if (res.ok) {
                const data = await res.json();
                this.#setLoggedIn(data.username, data.avatar || "");
                return true;
            }

            return false;
        } catch {
            // INFO: Network failure on boot — land on auth, let the user try manually
            storeNavigation.goto("auth");
            return false;
        }
    }

    /**
     * @brief Valida i campi lato client e invia la richiesta POST di registrazione.
     * @param username Nome utente desiderato.
     * @param email Indirizzo email.
     * @param password Password in chiaro.
     * @param confirmPassword Conferma della password.
     * @returns {Promise<AuthFieldErrors>} Oggetto con eventuali errori. Se vuoto, successo.
     */
    async register(
        username: string,
        email: string,
        password: string,
        confirmPassword: string
    ): Promise<AuthFieldErrors> {
        const errors: AuthFieldErrors = {};

        const usernameResult = validateUsername(username);
        if (!usernameResult.valid) errors.username = usernameResult.error;

        const emailResult = validateEmail(email);
        if (!emailResult.valid) errors.email = emailResult.error;

        const passwordResult = validatePassword(password);
        if (!passwordResult.valid) errors.password = passwordResult.error;

        const matchResult = validatePasswordMatch(password, confirmPassword);
        if (!matchResult.valid) errors.confirmPassword = matchResult.error;

        if (Object.keys(errors).length > 0) return errors;

        // --- Server round-trip ---
        this.isLoading = true;
        try {
            const res = await fetch("/auth/register", {
                method: "POST",
                headers: { "Content-Type": "application/json" },
                body: JSON.stringify({ username, email, password })
            });

            if (res.ok) {
                storeToast.success("Account created! You can now log in.");
                return {};
            }

            if (res.status === 409) {
                return { username: "Username or email is already taken." };
            }
            if (res.status === 422) {
                const body = await res.json().catch(() => ({}));
                return { username: body.error ?? "Invalid input." };
            }

            storeToast.error("Registration failed — please try again.");
            return {};
        } catch {
            storeToast.error("Network error — check your connection.");
            return {};
        } finally {
            this.isLoading = false;
        }
    }

    /**
     * @brief Valida le credenziali e tenta il login al server.
     * @param email Email inserita.
     * @param password Password inserita.
     * @returns {Promise<AuthFieldErrors>} Oggetto con eventuali errori (es. "Credenziali errate").
     */
    async login(email: string, password: string): Promise<AuthFieldErrors> {
        const errors: AuthFieldErrors = {};

        const emailResult = validateEmail(email);
        if (!emailResult.valid) errors.email = emailResult.error;
        if (password.length < 1) errors.password = "Password is required.";
        if (Object.keys(errors).length > 0) return errors;

        this.isLoading = true;

        try {
            const res = await fetch("/auth/login", {
                method: "POST",
                credentials: "include",
                headers: { "Content-Type": "application/json" },
                body: JSON.stringify({ email, password })
            });

            if (res.ok) {
                const data = await res.json();
                this.#setLoggedIn(data.username, data.avatar || "");
                return {};
            }

            // 401 is always "bad credentials" — don't leak which field is wrong
            if (res.status === 401) {
                return { email: "Incorrect email or password." };
            }

            storeToast.error("Login failed — please try again.");
            return {};
        } catch {
            storeToast.error("Network error — check your connection.");
            return {};
        } finally {
            this.isLoading = false;
        }
    }

    /**
     * @brief Aggiorna localmente l'avatar (es. Blob URL).
     * @param file Il file immagine selezionato.
     * @returns True se l'URL è stato creato con successo.
     */
    updateAvatar(file: File): boolean {
        try {
            this.avatar = URL.createObjectURL(file);
            storeToast.success("Immagine del profilo aggiornata localmente!");
            return true;
        } catch {
            storeToast.error("Impossibile caricare l'immagine.");
            return false;
        }
    }

    /**
     * @brief Tenta il logout inviando una richiesta e pulisce lo stato locale.
     */
    async logout(): Promise<void> {
        await fetch("/auth/logout", { method: "POST", credentials: "include" });
        this.#setLoggedOut();
        storeNavigation.goto("auth");
    }

    #setLoggedIn(username: string, avatar: string = ""): void {
        this.username = username;
        this.avatar = avatar;
        this.isLoggedIn = true;
    }

    #setLoggedOut(): void {
        this.username = "";
        this.avatar = "";
        this.isLoggedIn = false;
    }
}

export const storeAuth = new StoreAuth();
