import { describe, it, expect, vi, beforeEach } from "vitest";
import { render, screen, fireEvent } from "@testing-library/svelte";
import { storeAuth } from "$lib/stores/auth.svelte";
import ProfileLogout from "$components/profile/ProfileLogout.svelte";

vi.mock("$lib/stores/auth.svelte", () => ({
	storeAuth: {
		isLoggedIn: true,
		logout: vi.fn().mockResolvedValue(undefined)
	}
}));

describe("ProfileLogout", () => {
	beforeEach(() => {
		vi.mocked(storeAuth).isLoggedIn = true;
		vi.mocked(storeAuth).logout.mockReset();
		vi.mocked(storeAuth).logout.mockResolvedValue(undefined);
	});

	it("shows Logout for logged-in accounts and calls storeAuth.logout", async () => {
		render(ProfileLogout);
		const btn = screen.getByRole("button", { name: /logout/i });
		await fireEvent.click(btn);
		expect(storeAuth.logout).toHaveBeenCalledTimes(1);
	});

	it("hides Logout for guests", () => {
		vi.mocked(storeAuth).isLoggedIn = false;
		render(ProfileLogout);
		expect(screen.queryByRole("button", { name: /logout/i })).not.toBeInTheDocument();
	});
});
