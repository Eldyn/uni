import { describe, it, expect, vi } from "vitest";
import { render, screen } from "@testing-library/svelte";

vi.mock("$lib/stores/navigation.svelte", () => ({ storeNavigation: { goto: vi.fn(), openSettings: vi.fn() } }));
vi.mock("$lib/stores/auth.svelte", () => ({ storeAuth: { username: "eldyn", avatar: "" } }));

const { fetchMe } = vi.hoisted(() => ({ fetchMe: vi.fn() }));
vi.mock("$lib/stores/stats.svelte", () => ({
	storeStats: {
		myStats: { username: "eldyn", total_wins: 12, total_losses: 8, rank: 3 },
		isLoading: false,
		fetchMe
	}
}));

import { storeStats } from "$lib/stores/stats.svelte";
import ProfileScreen from "$components/profile/ProfileScreen.svelte";

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
});
