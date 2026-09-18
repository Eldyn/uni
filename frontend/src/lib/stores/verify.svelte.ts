/**
 * @file verify.svelte.ts
 * @brief Store for managing email verification requests, cooldown timers, and confirmation.
 */

import { storeAuth } from "./auth.svelte";
import { storeToast } from "./toast.svelte";
import { storeI18n } from "./i18n.svelte";
import * as m from "$lib/paraglide/messages.js";

/**
 * @class StoreVerify
 * @brief Manages email verification state, cooldown timers, code requests, and confirmation.
 */
class StoreVerify {
	/** True while requesting a verification code. */
	isSending = $state(false);
	/** True while verifying a submitted code. */
	isConfirming = $state(false);
	/** Seconds remaining before a new code can be requested. */
	cooldownSeconds = $state(0);
	/** Error message to display to the user on failure. */
	error = $state("");

	#intervalId: ReturnType<typeof setInterval> | undefined = undefined;
	#targetTimestamp: number | undefined = undefined;

	constructor() {
		storeAuth.onLoggedOut(() => {
			this.reset();
		});
	}

	#startCooldown(seconds = 60): void {
		if (this.#intervalId !== undefined) {
			clearInterval(this.#intervalId);
			this.#intervalId = undefined;
		}
		this.cooldownSeconds = seconds;
		this.#targetTimestamp = Date.now() + seconds * 1000;
		this.#intervalId = setInterval(() => {
			const remaining = Math.max(0, Math.ceil(((this.#targetTimestamp ?? 0) - Date.now()) / 1000));
			this.cooldownSeconds = remaining;
			if (remaining <= 0) {
				clearInterval(this.#intervalId);
				this.#intervalId = undefined;
				this.#targetTimestamp = undefined;
			}
		}, 1000);
	}

	/**
	 * @brief Requests a new 6-digit verification code sent via email.
	 * @returns True on success or if already verified, false otherwise.
	 */
	async requestCode(): Promise<boolean> {
		if (this.cooldownSeconds > 0 || this.isSending) {
			return false;
		}

		this.isSending = true;
		this.error = "";

		try {
			const res = await fetch("/auth/verify/request-code", {
				method: "POST",
				credentials: "include",
				headers: { "Content-Type": "application/json" },
				body: JSON.stringify({ locale: storeI18n.locale })
			});

			if (res.status === 202) {
				this.#startCooldown(60);
				return true;
			}

			if (res.status === 409) {
				this.dispose();
				this.cooldownSeconds = 0;
				storeAuth.emailVerified = true;
				return true;
			}

			if (res.status === 429) {
				this.error = m.error_verify_rate_limited({}, { locale: storeI18n.locale });
				return false;
			}

			const body = await res.json().catch(() => ({}));
			const errorMsg = body.error ?? "Failed to send verification code, please try again.";
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
	 * @brief Submits a 6-digit code to verify the account's email address.
	 * @param code 6-digit verification code.
	 * @param email Optional email address when confirming outside an active session.
	 * @returns True on successful verification, false otherwise.
	 */
	async confirmCode(code: string, email?: string): Promise<boolean> {
		if (this.isConfirming) {
			return false;
		}

		this.isConfirming = true;
		this.error = "";

		try {
			const payload: { code: string; email?: string } = { code };
			if (email) {
				payload.email = email;
			}

			const res = await fetch("/auth/verify/confirm-code", {
				method: "POST",
				credentials: "include",
				headers: { "Content-Type": "application/json" },
				body: JSON.stringify(payload)
			});

			if (res.status === 200) {
				this.dispose();
				this.cooldownSeconds = 0;
				storeAuth.emailVerified = true;
				storeToast.success(m.verify_success_toast({}, { locale: storeI18n.locale }));
				return true;
			}

			if (res.status === 401) {
				this.error = m.error_verify_wrong_code({}, { locale: storeI18n.locale });
				return false;
			}

			if (res.status === 429) {
				this.error = m.error_verify_too_many_attempts({}, { locale: storeI18n.locale });
				return false;
			}

			const data = await res.json().catch(() => ({}));
			this.error = data.error ?? "Failed to verify code.";
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
	 * @brief Clears active cooldown timers for cleanup during component/test disposal.
	 */
	dispose(): void {
		if (this.#intervalId !== undefined) {
			clearInterval(this.#intervalId);
			this.#intervalId = undefined;
		}
		this.#targetTimestamp = undefined;
	}

	/**
	 * @brief Resets all verify store state and timers (e.g. on user logout).
	 */
	reset(): void {
		this.dispose();
		this.cooldownSeconds = 0;
		this.error = "";
		this.isSending = false;
		this.isConfirming = false;
	}
}

export const storeVerify = new StoreVerify();
