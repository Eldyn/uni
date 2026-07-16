/**
 * @file auth.svelte.ts
 * @brief Store for managing authentication, session and validation.
 * Concentrates all the RESTful HTTP calls to the `/auth` module and maps
 * server errors to valid messages for the interface forms.
 */

import {
	validateEmail,
	validatePassword,
	validateUsername,
	validatePasswordMatch
} from "$utils/validation";
import { storeAnalytics } from "./analytics.svelte";
import { storeNavigation } from "./navigation.svelte";
import { storeToast } from "./toast.svelte";
import { ws } from "./ws.svelte";

/**
 * @interface AuthFieldErrors
 * @brief Mapping of the errors for each individual field of the registration/login form.
 */
export interface AuthFieldErrors {
	username?: string;
	email?: string;
	password?: string;
	confirmPassword?: string;
}

/**
 * @class StoreAuth
 * @brief Manages the reactive state of the authenticated user (Username, Avatar).
 * @tag FRONT-AUTH-001
 */
class StoreAuth {
	/** Username of the currently connected account. */
	username = $state("");
	/** Profile picture or local avatar URL. */
	avatar = $state("");
	/** Flag indicating whether the user holds a valid session. */
	isLoggedIn = $state(false);
	/** True when playing with an ephemeral guest session (no account). */
	isGuest = $state(false);
	/** Flag to show the loading indicators (spinners) during HTTP calls. */
	isLoading = $state(false);

	/**
	 * @brief Called on app startup to restore a pre-existing session.
	 * Leverages the automatic sending of HttpOnly cookies to the `/auth/me` endpoint.
	 * @returns {Promise<boolean>} True if the user is logged in, false otherwise.
	 */
	async checkSession(): Promise<boolean> {
		try {
			// Prefer the prefetch kicked off inline in index.html (runs in parallel
			// with the JS bundle download). Fall back to a fresh request if absent.
			const prefetched = (window as unknown as { __authMe?: Promise<unknown> }).__authMe;
			const data = prefetched
				? await prefetched
				: await fetch("/auth/me", {
						credentials: "include",
						headers: { "Content-Type": "application/json" }
					}).then((res) => (res.ok ? res.json() : null));

			if (data && typeof data === "object") {
				const { username, avatar } = data as { username: string; avatar?: string };
				this.#setLoggedIn(username, avatar || "");
				return true;
			}

			return false;
		} catch {
			// INFO: Network failure on boot, open the auth modal, let the user try manually
			storeNavigation.gotoAuth("login");
			return false;
		}
	}

	/**
	 * @brief Validates the fields client-side and sends the POST registration request.
	 * @param username Desired username.
	 * @param email Email address.
	 * @param password Plaintext password.
	 * @param confirmPassword Password confirmation.
	 * @returns {Promise<AuthFieldErrors>} Object with any errors. If empty, success.
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
				storeAnalytics.track("sign_up");
				storeToast.success("Account created! You can now log in.");
				return {};
			}

			storeAnalytics.track("auth_error", { method: "register", reason: String(res.status) });

			if (res.status === 409) {
				return { username: "Username or email is already taken." };
			}
			if (res.status === 422) {
				const body = await res.json().catch(() => ({}));
				return { username: body.error ?? "Invalid input." };
			}
			// 429: per-IP auth rate limiter (shared across all /auth/* routes).
			if (res.status === 429) {
				const body = await res.json().catch(() => ({}));
				return {
					username: body.error ?? "Too many attempts, please wait and try again."
				};
			}

			storeToast.error("Registration failed, please try again.");
			return {};
		} catch {
			storeToast.error("Network error, check your connection.");
			return {};
		} finally {
			this.isLoading = false;
		}
	}

	/**
	 * @brief Validates the credentials and attempts to log in to the server.
	 * @param email Entered email.
	 * @param password Entered password.
	 * @returns {Promise<AuthFieldErrors>} Object with any errors (e.g. "Incorrect credentials").
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
				await this.#resyncSocket();
				storeAnalytics.track("login");
				return {};
			}

			storeAnalytics.track("auth_error", { method: "login", reason: String(res.status) });

			// 401 is always "bad credentials", don't leak which field is wrong
			if (res.status === 401) {
				return { email: "Incorrect email or password." };
			}

			// 429: per-(email,ip) lockout after repeated failures, or the
			// per-IP auth rate limiter. Surface the server's own message.
			if (res.status === 429) {
				const body = await res.json().catch(() => ({}));
				return {
					email: body.error ?? "Too many attempts, please wait and try again."
				};
			}

			storeToast.error("Login failed, please try again.");
			return {};
		} catch {
			storeToast.error("Network error, check your connection.");
			return {};
		} finally {
			this.isLoading = false;
		}
	}

	/**
	 * @brief Starts (or reuses) an ephemeral guest session.
	 * The server issues a generated display name and the ws_token cookie
	 * needed for the WebSocket upgrade; no account is created.
	 * @returns True when a guest session is active.
	 */
	async loginAsGuest(): Promise<boolean> {
		if (this.isGuest && this.username) return true;

		this.isLoading = true;
		try {
			const res = await fetch("/auth/guest", { method: "POST", credentials: "include" });
			if (!res.ok) {
				storeToast.error("Could not start a guest session, please try again.");
				return false;
			}
			const data = await res.json();
			this.username = data.username;
			this.isGuest = true;
			await this.#resyncSocket();
			storeAnalytics.track("guest_session");
			return true;
		} catch {
			storeToast.error("Network error, check your connection.");
			return false;
		} finally {
			this.isLoading = false;
		}
	}

	/**
	 * @brief Updates the avatar locally (e.g. Blob URL).
	 * @param file The selected image file.
	 * @returns True if the URL was created successfully.
	 */
	updateAvatar(file: File): boolean {
		try {
			const prev = this.avatar;
			this.avatar = URL.createObjectURL(file);
			if (prev.startsWith("blob:")) URL.revokeObjectURL(prev);
			storeToast.success("Profile picture updated locally!");
			return true;
		} catch {
			storeToast.error("Unable to load the image.");
			return false;
		}
	}

	/**
	 * @brief Attempts to log out by sending a request and clears the local state.
	 */
	async logout(): Promise<void> {
		const res = await fetch("/auth/logout", { method: "POST", credentials: "include" });
		if (!res.ok) {
			storeToast.error("Logout failed, please try again.");
			return;
		}
		this.#setLoggedOut();
		await this.#resyncSocket();
		storeAnalytics.track("logout");
		storeNavigation.goto("main");
		storeNavigation.gotoAuth("login");
	}

	/**
	 * Login/guest-login rotates the `ws_token` cookie, but an already-open
	 * socket keeps the identity it upgraded with, forces a reconnect so the
	 * next handshake picks up the new cookie (otherwise chat/lobby traffic
	 * keeps going out under the previous guest/account name).
	 */
	async #resyncSocket(): Promise<void> {
		if (!ws.isConnected) return;
		ws.disconnect(1000, "identity changed");
		await ws.connect();
	}

	#setLoggedIn(username: string, avatar: string = ""): void {
		this.username = username;
		this.avatar = avatar;
		this.isLoggedIn = true;
		this.isGuest = false;
	}

	#setLoggedOut(): void {
		this.username = "";
		this.avatar = "";
		this.isLoggedIn = false;
		this.isGuest = false;
	}
}

export const storeAuth = new StoreAuth();
