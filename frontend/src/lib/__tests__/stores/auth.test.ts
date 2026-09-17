import { describe, it, expect, vi, beforeEach } from "vitest";
import { storeAuth } from "$stores/auth.svelte";
import { storeToast } from "$stores/toast.svelte";
import { storeAnalytics } from "$stores/analytics.svelte";

vi.mock("$stores/toast.svelte", () => ({
	storeToast: { success: vi.fn(), error: vi.fn() }
}));

vi.mock("$stores/analytics.svelte", () => ({
	storeAnalytics: { track: vi.fn() }
}));

global.fetch = vi.fn();

describe("auth store", () => {
	beforeEach(() => {
		vi.resetAllMocks();
	});

	it("logs the user in immediately after successful registration", async () => {
		vi.mocked(fetch).mockResolvedValueOnce(
			new Response(JSON.stringify({ status: "ok", username: "newuser", email_verified: false }), {
				status: 200
			})
		);
		const result = await storeAuth.register(
			"newuser",
			"new@example.com",
			"password123",
			"password123"
		);

		// Result should be empty object (no errors)
		expect(Object.keys(result).length).toBe(0);
		expect(storeAuth.isLoggedIn).toBe(true);
		expect(storeAuth.username).toBe("newuser");
		expect(storeAuth.emailVerified).toBe(false);
	});

	it("checkSession sets emailVerified from /auth/me", async () => {
		vi.mocked(fetch).mockResolvedValueOnce(
			new Response(JSON.stringify({ username: "verifieduser", avatar: "", email_verified: true }), {
				status: 200
			})
		);
		const ok = await storeAuth.checkSession();
		expect(ok).toBe(true);
		expect(storeAuth.isLoggedIn).toBe(true);
		expect(storeAuth.username).toBe("verifieduser");
		expect(storeAuth.emailVerified).toBe(true);
	});

	it("setLoggedOut resets emailVerified to false", () => {
		storeAuth.emailVerified = true;
		storeAuth.isLoggedIn = true;
		storeAuth.setLoggedOut();
		expect(storeAuth.emailVerified).toBe(false);
		expect(storeAuth.isLoggedIn).toBe(false);
	});
});
