/**
 * Owns all fetch calls to /auth/* and is the single source of truth for
 * the current session. Validation lives in validation.ts; this store
 * calls those helpers and converts server error codes to field messages.
 */

import {
    validateEmail,
    validatePassword,
    validateUsername,
    validatePasswordMatch
} from "../utils/validation";
import { storeNavigation } from "./navigation.svelte";
import { storeToast } from "./toast.svelte";

export interface AuthFieldErrors {
    username?: string;
    email?: string;
    password?: string;
    confirmPassword?: string;
}

class StoreAuth {
    username = $state("");
    avatar = $state(""); // Gestito localmente come URL string
    isLoggedIn = $state(false);
    isLoading = $state(false);

    /**
     * Called once at app boot, restores an existing session from the
     * HttpOnly cookie. Navigates to "auth" or "lobbies" accordingly.
     * @returns whether or not we are logged in or not.
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
     * Validate fields client-side, then POST /auth/register.
     * On success, shows a toast
     * @returns Promise<AuthFieldErrors> an errors object — empty means success.
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
     * Validate fields, then POST /auth/login.
     * On success, updates reactive state.
     * @returns Promise<AuthFieldErrors> an errors object — empty means success.
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
     * Aggiorna l'avatar nello stato locale del client.
     * Evita chiamate HTTP a endpoint inesistenti (404).
     * @param file Il file nativo proveniente dall'input HTML
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
     * Attempts a logout.
     * @remarks even if the server request fails, the client logs out.
     */
    async logout(): Promise<void> {
        await fetch("/auth/logout", { method: "POST", credentials: "include" });
        // BUG: this does not work
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
