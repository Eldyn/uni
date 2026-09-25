import { describe, it, expect, vi, beforeEach } from "vitest";
import { storeReset } from "$stores/reset.svelte";
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

describe("storeReset", () => {
	beforeEach(() => {
		vi.resetAllMocks();
		storeReset.reset();
	});

	describe("requestReset", () => {
		it("sets sent on 202 and posts email + locale", async () => {
			vi.mocked(fetch).mockResolvedValueOnce(new Response(null, { status: 202 }));
			const ok = await storeReset.requestReset("a@example.com");

			expect(ok).toBe(true);
			expect(storeReset.sent).toBe(true);
			expect(storeReset.isSending).toBe(false);
			expect(storeReset.error).toBe("");
			expect(fetch).toHaveBeenCalledWith("/auth/reset/request", {
				method: "POST",
				credentials: "include",
				headers: { "Content-Type": "application/json" },
				body: JSON.stringify({ email: "a@example.com", locale: "en" })
			});
		});

		it("is generic: 202 for an unknown email is also success", async () => {
			vi.mocked(fetch).mockResolvedValueOnce(new Response(null, { status: 202 }));
			const ok = await storeReset.requestReset("nobody@example.com");
			expect(ok).toBe(true);
			expect(storeReset.sent).toBe(true);
		});

		it("is a no-op while isSending is true", async () => {
			storeReset.isSending = true;
			const ok = await storeReset.requestReset("a@example.com");
			expect(ok).toBe(false);
			expect(vi.mocked(fetch)).not.toHaveBeenCalled();
		});

		it("handles a server failure", async () => {
			vi.mocked(fetch).mockResolvedValueOnce(
				new Response(JSON.stringify({ error: "Server error" }), { status: 500 })
			);
			const ok = await storeReset.requestReset("a@example.com");
			expect(ok).toBe(false);
			expect(storeReset.sent).toBe(false);
			expect(storeReset.error).toBe("Server error");
			expect(storeToast.error).toHaveBeenCalled();
		});

		it("handles a network exception", async () => {
			vi.mocked(fetch).mockRejectedValueOnce(new Error("Network failed"));
			const ok = await storeReset.requestReset("a@example.com");
			expect(ok).toBe(false);
			expect(storeReset.error).toBe("Network error, check your connection.");
			expect(storeToast.error).toHaveBeenCalledWith("Network error, check your connection.");
		});
	});

	describe("confirmReset", () => {
		it("returns true and toasts on 200", async () => {
			vi.mocked(fetch).mockResolvedValueOnce(
				new Response(JSON.stringify({ status: "ok" }), { status: 200 })
			);
			const ok = await storeReset.confirmReset("tok", "newpassword1");

			expect(ok).toBe(true);
			expect(storeReset.isConfirming).toBe(false);
			expect(storeToast.success).toHaveBeenCalledWith(
				m.reset_success_toast({}, { locale: storeI18n.locale })
			);
			expect(fetch).toHaveBeenCalledWith("/auth/reset/confirm", {
				method: "POST",
				credentials: "include",
				headers: { "Content-Type": "application/json" },
				body: JSON.stringify({ token: "tok", password: "newpassword1" })
			});
		});

		it("sets invalid-link error on 401", async () => {
			vi.mocked(fetch).mockResolvedValueOnce(new Response(null, { status: 401 }));
			const ok = await storeReset.confirmReset("tok", "newpassword1");
			expect(ok).toBe(false);
			expect(storeReset.error).toBe(m.error_reset_invalid_link({}, { locale: storeI18n.locale }));
		});

		it("sets weak-password error on 400", async () => {
			vi.mocked(fetch).mockResolvedValueOnce(new Response(null, { status: 400 }));
			const ok = await storeReset.confirmReset("tok", "short");
			expect(ok).toBe(false);
			expect(storeReset.error).toBe(m.error_reset_weak_password({}, { locale: storeI18n.locale }));
		});

		it("is a no-op while isConfirming is true", async () => {
			storeReset.isConfirming = true;
			const ok = await storeReset.confirmReset("tok", "newpassword1");
			expect(ok).toBe(false);
			expect(vi.mocked(fetch)).not.toHaveBeenCalled();
		});

		it("handles a network exception", async () => {
			vi.mocked(fetch).mockRejectedValueOnce(new Error("Network failed"));
			const ok = await storeReset.confirmReset("tok", "newpassword1");
			expect(ok).toBe(false);
			expect(storeToast.error).toHaveBeenCalledWith("Network error, check your connection.");
		});
	});

	describe("reset", () => {
		it("clears all state", () => {
			storeReset.sent = true;
			storeReset.error = "boom";
			storeReset.isSending = true;
			storeReset.isConfirming = true;

			storeReset.reset();

			expect(storeReset.sent).toBe(false);
			expect(storeReset.error).toBe("");
			expect(storeReset.isSending).toBe(false);
			expect(storeReset.isConfirming).toBe(false);
		});
	});
});
