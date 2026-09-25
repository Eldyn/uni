/**
 * @file reset.svelte.ts
 * @brief Store for the password-reset (magic link) flow: requesting a reset
 * link by email and confirming a new password with a token.
 */

import { storeAuth } from "./auth.svelte";
import { storeToast } from "./toast.svelte";
import { storeI18n } from "./i18n.svelte";
import * as m from "$lib/paraglide/messages.js";

/**
 * @class StoreReset
 * @brief Manages password-reset request/confirm state and generic responses.
 */
class StoreReset {
	/** True while requesting a reset link. */
	isSending = $state(false);
	/** True while confirming a new password. */
	isConfirming = $state(false);
	/** True once a request has been accepted (generic confirmation). */
	sent = $state(false);
	/** Error message to display to the user on failure. */
	error = $state("");

	constructor() {
		storeAuth.onLoggedOut(() => {
			this.reset();
		});
	}

	/**
	 * @brief Requests a reset link for the given email address.
	 * Always generic: a 202 (including for unknown emails/over-cap) counts as
	 * success, so the UI never discloses whether an account exists.
	 * @returns True when the request was accepted, false on a hard failure.
	 */
	async requestReset(email: string): Promise<boolean> {
		if (this.isSending) {
			return false;
		}

		this.isSending = true;
		this.error = "";

		try {
			const res = await fetch("/auth/reset/request", {
				method: "POST",
				credentials: "include",
				headers: { "Content-Type": "application/json" },
				body: JSON.stringify({ email, locale: storeI18n.locale })
			});

			if (res.status === 202) {
				this.sent = true;
				return true;
			}

			const body = await res.json().catch(() => ({}));
			const errorMsg = body.error ?? "Failed to request a reset link, please try again.";
			this.error = errorMsg;
			storeToast.error(errorMsg);
			return false;
		} catch {
			const networkErr = "Network error, check your connection.";
			this.error = networkErr;
			storeToast.error(networkErr);
			return false;
		} finally {
			this.isSending = false;
		}
	}

	/**
	 * @brief Confirms a reset token and sets a new password.
	 * @param token Magic-link token.
	 * @param password New plaintext password.
	 * @returns True on success, false otherwise.
	 */
	async confirmReset(token: string, password: string): Promise<boolean> {
		if (this.isConfirming) {
			return false;
		}

		this.isConfirming = true;
		this.error = "";

		try {
			const res = await fetch("/auth/reset/confirm", {
				method: "POST",
				credentials: "include",
				headers: { "Content-Type": "application/json" },
				body: JSON.stringify({ token, password })
			});

			if (res.status === 200) {
				storeToast.success(m.reset_success_toast({}, { locale: storeI18n.locale }));
				return true;
			}

			if (res.status === 401) {
				this.error = m.error_reset_invalid_link({}, { locale: storeI18n.locale });
				return false;
			}

			if (res.status === 400) {
				this.error = m.error_reset_weak_password({}, { locale: storeI18n.locale });
				return false;
			}

			const data = await res.json().catch(() => ({}));
			this.error = data.error ?? "Failed to reset password.";
			return false;
		} catch {
			const networkErr = "Network error, check your connection.";
			this.error = networkErr;
			storeToast.error(networkErr);
			return false;
		} finally {
			this.isConfirming = false;
		}
	}

	/**
	 * @brief Resets all store state (e.g. on logout or modal close).
	 */
	reset(): void {
		this.error = "";
		this.sent = false;
		this.isSending = false;
		this.isConfirming = false;
	}
}

export const storeReset = new StoreReset();
