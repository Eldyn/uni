import { describe, it, expect, vi } from "vitest";
import { render, screen, fireEvent } from "@testing-library/svelte";
import { storeAuth } from "$lib/stores/auth.svelte";
import { storeStats } from "$lib/stores/stats.svelte";
import ProfileScreen from "$components/profile/ProfileScreen.svelte";

vi.mock("$lib/stores/navigation.svelte", () => ({
	storeNavigation: { goto: vi.fn(), openSettings: vi.fn() }
}));
vi.mock("$lib/stores/auth.svelte", () => ({
	storeAuth: {
		username: "eldyn",
		avatar: "",
		isLoggedIn: true,
		emailVerified: false,
		onLoggedOut: vi.fn()
	}
}));

const { fetchMe } = vi.hoisted(() => ({ fetchMe: vi.fn() }));
vi.mock("$lib/stores/stats.svelte", () => ({
	storeStats: {
		myStats: { username: "eldyn", total_wins: 12, total_losses: 8, rank: 3 },
		isLoading: false,
		fetchMe
	}
}));

describe("ProfileScreen", () => {
	it("shows the player's username and a stat summary", () => {
		render(ProfileScreen);
		expect(screen.getByText("eldyn")).toBeInTheDocument();
		expect(screen.getByText(/12/)).toBeInTheDocument();
	});

	it("offers entry points to detailed stats and settings", () => {
		render(ProfileScreen);
		expect(screen.getByRole("button", { name: /leaderboard|detailed stats/i })).toBeInTheDocument();
		expect(screen.getByRole("button", { name: /settings/i })).toBeInTheDocument();
	});

	it("fetches stats on mount when they haven't loaded yet", () => {
		fetchMe.mockClear();
		(storeStats as { myStats: unknown }).myStats = null;
		render(ProfileScreen);
		expect(fetchMe).toHaveBeenCalled();
		(storeStats as { myStats: unknown }).myStats = {
			username: "eldyn",
			total_wins: 12,
			total_losses: 8,
			rank: 3
		};
	});

	it("shows VERIFY button for unverified logged-in accounts", () => {
		vi.mocked(storeAuth).isLoggedIn = true;
		vi.mocked(storeAuth).emailVerified = false;
		render(ProfileScreen);
		expect(screen.getByRole("button", { name: /verify/i })).toBeInTheDocument();
	});

	it("hides VERIFY button for verified accounts and guests", () => {
		vi.mocked(storeAuth).isLoggedIn = true;
		vi.mocked(storeAuth).emailVerified = true;
		render(ProfileScreen);
		expect(screen.queryByRole("button", { name: /verify/i })).not.toBeInTheDocument();
	});

	it("toggles verify code form when clicking VERIFY button", async () => {
		vi.mocked(storeAuth).isLoggedIn = true;
		vi.mocked(storeAuth).emailVerified = false;
		render(ProfileScreen);
		const btn = screen.getByRole("button", { name: /verify/i });
		expect(screen.queryByTestId("verify-code-form")).not.toBeInTheDocument();
		await fireEvent.click(btn);
		expect(screen.getByTestId("verify-code-form")).toBeInTheDocument();
		await fireEvent.click(btn);
		expect(screen.queryByTestId("verify-code-form")).not.toBeInTheDocument();
	});
});
