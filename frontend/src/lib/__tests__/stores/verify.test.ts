import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { storeVerify } from "$stores/verify.svelte";
import { storeAuth } from "$stores/auth.svelte";
import { storeToast } from "$stores/toast.svelte";
import { storeI18n } from "$stores/i18n.svelte";
import * as m from "$lib/paraglide/messages.js";

vi.mock("$stores/toast.svelte", () => ({
	storeToast: {
		success: vi.fn(),
		error: vi.fn()
	}
}));

vi.mock("$stores/i18n.svelte", () => ({
	storeI18n: {
		locale: "en"
	}
}));

global.fetch = vi.fn();

describe("storeVerify", () => {
	beforeEach(() => {
		vi.useFakeTimers();
		vi.resetAllMocks();
		storeVerify.dispose();
		storeVerify.cooldownSeconds = 0;
		storeVerify.isSending = false;
		storeVerify.isConfirming = false;
		storeVerify.error = "";
		storeAuth.emailVerified = false;
	});

	afterEach(() => {
		storeVerify.dispose();
		vi.useRealTimers();
	});

	describe("requestCode", () => {
		it("requestCode starts a 60s cooldown on success (202)", async () => {
			vi.mocked(fetch).mockResolvedValueOnce(new Response(null, { status: 202 }));
			const promise = storeVerify.requestCode();
			expect(storeVerify.isSending).toBe(true);

			const ok = await promise;
			expect(ok).toBe(true);
			expect(storeVerify.isSending).toBe(false);
			expect(storeVerify.cooldownSeconds).toBe(60);
			expect(storeVerify.error).toBe("");

			// Ticking down
			vi.advanceTimersByTime(1000);
			expect(storeVerify.cooldownSeconds).toBe(59);

			vi.advanceTimersByTime(59000);
			expect(storeVerify.cooldownSeconds).toBe(0);

			// Should not go below 0
			vi.advanceTimersByTime(1000);
			expect(storeVerify.cooldownSeconds).toBe(0);
		});

		it("computes cooldown accurately when interval is throttled in inactive tab", async () => {
			vi.mocked(fetch).mockResolvedValueOnce(new Response(null, { status: 202 }));
			await storeVerify.requestCode();
			expect(storeVerify.cooldownSeconds).toBe(60);

			// Simulate throttled interval in background tab: 15s jump at once
			vi.advanceTimersByTime(15000);
			expect(storeVerify.cooldownSeconds).toBe(45);

			// Jump past remaining duration
			vi.advanceTimersByTime(50000);
			expect(storeVerify.cooldownSeconds).toBe(0);

			// Interval was cleared, stays 0
			vi.advanceTimersByTime(5000);
			expect(storeVerify.cooldownSeconds).toBe(0);
		});

		it("requestCode is a no-op while cooling down", async () => {
			storeVerify.cooldownSeconds = 30;
			const spy = vi.mocked(fetch);
			const ok = await storeVerify.requestCode();
			expect(ok).toBe(false);
			expect(spy).not.toHaveBeenCalled();
		});

		it("requestCode is a no-op while isSending is true", async () => {
			storeVerify.isSending = true;
			const spy = vi.mocked(fetch);
			const ok = await storeVerify.requestCode();
			expect(ok).toBe(false);
			expect(spy).not.toHaveBeenCalled();
		});

		it("sends correct POST request with credentials and locale payload", async () => {
			vi.mocked(fetch).mockResolvedValueOnce(new Response(null, { status: 202 }));
			await storeVerify.requestCode();

			expect(fetch).toHaveBeenCalledWith("/auth/verify/request-code", {
				method: "POST",
				credentials: "include",
				headers: { "Content-Type": "application/json" },
				body: JSON.stringify({ locale: "en" })
			});
		});

		it("sets rate-limited error on 429 and returns false without cooldown", async () => {
			vi.mocked(fetch).mockResolvedValueOnce(new Response(null, { status: 429 }));
			const ok = await storeVerify.requestCode();

			expect(ok).toBe(false);
			expect(storeVerify.cooldownSeconds).toBe(0);
			expect(storeVerify.error).toBe(m.error_verify_rate_limited({}, { locale: storeI18n.locale }));
			expect(storeVerify.isSending).toBe(false);
		});

		it("sets storeAuth.emailVerified = true on 409 conflict and returns true", async () => {
			vi.mocked(fetch).mockResolvedValueOnce(new Response(null, { status: 409 }));
			const ok = await storeVerify.requestCode();

			expect(ok).toBe(true);
			expect(storeAuth.emailVerified).toBe(true);
			expect(storeVerify.cooldownSeconds).toBe(0);
			expect(storeVerify.isSending).toBe(false);
		});

		it("handles server failure and surfaces error toast", async () => {
			vi.mocked(fetch).mockResolvedValueOnce(
				new Response(JSON.stringify({ error: "Server error" }), { status: 500 })
			);
			const ok = await storeVerify.requestCode();

			expect(ok).toBe(false);
			expect(storeToast.error).toHaveBeenCalled();
			expect(storeVerify.isSending).toBe(false);
		});

		it("handles network exception and surfaces toast error", async () => {
			vi.mocked(fetch).mockRejectedValueOnce(new Error("Network failed"));
			const ok = await storeVerify.requestCode();

			expect(ok).toBe(false);
			expect(storeToast.error).toHaveBeenCalledWith("Network error, check your connection.");
			expect(storeVerify.isSending).toBe(false);
		});
	});

	describe("confirmCode", () => {
		it("confirmCode is a no-op while isConfirming is true", async () => {
			storeVerify.isConfirming = true;
			const spy = vi.mocked(fetch);
			const ok = await storeVerify.confirmCode("123456");

			expect(ok).toBe(false);
			expect(spy).not.toHaveBeenCalled();
		});

		it("confirmCode sets emailVerified on 200", async () => {
			vi.mocked(fetch).mockResolvedValueOnce(
				new Response(JSON.stringify({ status: "ok", email_verified: true }), { status: 200 })
			);
			const promise = storeVerify.confirmCode("123456");
			expect(storeVerify.isConfirming).toBe(true);

			const ok = await promise;
			expect(ok).toBe(true);
			expect(storeVerify.isConfirming).toBe(false);
			expect(storeAuth.emailVerified).toBe(true);
			expect(storeToast.success).toHaveBeenCalledWith(
				m.verify_success_toast({}, { locale: storeI18n.locale })
			);
			expect(fetch).toHaveBeenCalledWith("/auth/verify/confirm-code", {
				method: "POST",
				credentials: "include",
				headers: { "Content-Type": "application/json" },
				body: JSON.stringify({ code: "123456" })
			});
		});

		it("includes email in payload if provided", async () => {
			vi.mocked(fetch).mockResolvedValueOnce(
				new Response(JSON.stringify({ status: "ok", email_verified: true }), { status: 200 })
			);
			await storeVerify.confirmCode("123456", "test@example.com");

			expect(fetch).toHaveBeenCalledWith("/auth/verify/confirm-code", {
				method: "POST",
				credentials: "include",
				headers: { "Content-Type": "application/json" },
				body: JSON.stringify({ code: "123456", email: "test@example.com" })
			});
		});

		it("sets error to wrong code on 401", async () => {
			vi.mocked(fetch).mockResolvedValueOnce(new Response(null, { status: 401 }));
			const ok = await storeVerify.confirmCode("123456");

			expect(ok).toBe(false);
			expect(storeVerify.isConfirming).toBe(false);
			expect(storeVerify.error).toBe(m.error_verify_wrong_code({}, { locale: storeI18n.locale }));
		});

		it("sets error to too many attempts on 429", async () => {
			vi.mocked(fetch).mockResolvedValueOnce(new Response(null, { status: 429 }));
			const ok = await storeVerify.confirmCode("123456");

			expect(ok).toBe(false);
			expect(storeVerify.isConfirming).toBe(false);
			expect(storeVerify.error).toBe(
				m.error_verify_too_many_attempts({}, { locale: storeI18n.locale })
			);
		});

		it("handles other HTTP failures", async () => {
			vi.mocked(fetch).mockResolvedValueOnce(
				new Response(JSON.stringify({ error: "Some error" }), { status: 400 })
			);
			const ok = await storeVerify.confirmCode("123456");

			expect(ok).toBe(false);
			expect(storeVerify.isConfirming).toBe(false);
			expect(storeVerify.error).toBe("Some error");
		});

		it("handles network exception on confirmCode", async () => {
			vi.mocked(fetch).mockRejectedValueOnce(new Error("Network failed"));
			const ok = await storeVerify.confirmCode("123456");

			expect(ok).toBe(false);
			expect(storeVerify.isConfirming).toBe(false);
			expect(storeToast.error).toHaveBeenCalledWith("Network error, check your connection.");
			expect(storeVerify.error).toBe("Network error, check your connection.");
		});
		it("confirmCode clears active cooldown timer on 200", async () => {
			vi.mocked(fetch).mockResolvedValueOnce(new Response(null, { status: 202 }));
			await storeVerify.requestCode();
			expect(storeVerify.cooldownSeconds).toBe(60);

			vi.advanceTimersByTime(10000);
			expect(storeVerify.cooldownSeconds).toBe(50);

			vi.mocked(fetch).mockResolvedValueOnce(
				new Response(JSON.stringify({ status: "ok", email_verified: true }), { status: 200 })
			);
			const ok = await storeVerify.confirmCode("123456");
			expect(ok).toBe(true);
			expect(storeVerify.cooldownSeconds).toBe(0);

			// Timer cancelled, stays 0
			vi.advanceTimersByTime(5000);
			expect(storeVerify.cooldownSeconds).toBe(0);
		});
	});

	describe("dispose", () => {
		it("clears the cooldown interval", async () => {
			vi.mocked(fetch).mockResolvedValueOnce(new Response(null, { status: 202 }));
			await storeVerify.requestCode();
			expect(storeVerify.cooldownSeconds).toBe(60);

			storeVerify.dispose();

			vi.advanceTimersByTime(5000);
			// Timer was cancelled, so seconds do not tick down anymore
			expect(storeVerify.cooldownSeconds).toBe(60);
		});
	});

	describe("session cleanup and reset", () => {
		it("resets store state and clears cooldown timer on reset()", async () => {
			vi.mocked(fetch).mockResolvedValueOnce(new Response(null, { status: 202 }));
			await storeVerify.requestCode();
			expect(storeVerify.cooldownSeconds).toBe(60);

			storeVerify.error = "Previous error";
			storeVerify.isSending = true;
			storeVerify.isConfirming = true;

			storeVerify.reset();

			expect(storeVerify.cooldownSeconds).toBe(0);
			expect(storeVerify.error).toBe("");
			expect(storeVerify.isSending).toBe(false);
			expect(storeVerify.isConfirming).toBe(false);

			// Interval cancelled, won't tick
			vi.advanceTimersByTime(5000);
			expect(storeVerify.cooldownSeconds).toBe(0);
		});

		it("resets store state when user logs out via storeAuth", async () => {
			vi.mocked(fetch).mockResolvedValueOnce(new Response(null, { status: 202 }));
			await storeVerify.requestCode();
			expect(storeVerify.cooldownSeconds).toBe(60);

			storeVerify.error = "Some error";
			storeAuth.setLoggedOut();

			expect(storeVerify.cooldownSeconds).toBe(0);
			expect(storeVerify.error).toBe("");

			// Timer cancelled
			vi.advanceTimersByTime(5000);
			expect(storeVerify.cooldownSeconds).toBe(0);
		});
	});
});
