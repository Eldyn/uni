import { describe, it, expect, vi, beforeEach } from "vitest";
import { render, screen, fireEvent } from "@testing-library/svelte";
import { storeAuth } from "$lib/stores/auth.svelte";
import { storeStats } from "$lib/stores/stats.svelte";
import { storeNavigation } from "$lib/stores/navigation.svelte";
import { storeVerify } from "$stores/verify.svelte";
import ProfileScreen from "$components/profile/ProfileScreen.svelte";

const defaultStats = { username: "eldyn", total_wins: 12, total_losses: 8, rank: 3 };

vi.mock("$lib/stores/navigation.svelte", () => ({
	storeNavigation: {
		goto: vi.fn(),
		gotoAuth: vi.fn(),
		openSettings: vi.fn(),
		activeVerifyCode: null
	}
}));
vi.mock("$lib/stores/auth.svelte", () => ({
	storeAuth: {
		username: "eldyn",
		avatar: "",
		isLoggedIn: true,
		emailVerified: false,
		logout: vi.fn().mockResolvedValue(undefined),
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

const { fetchMine } = vi.hoisted(() => ({ fetchMine: vi.fn().mockResolvedValue(undefined) }));
vi.mock("$stores/avatar.svelte", () => ({
	storeAvatar: {
		loadout: null,
		fetchMine
	}
}));

vi.mock("$stores/verify.svelte", () => ({
	storeVerify: {
		isSending: false,
		isConfirming: false,
		cooldownSeconds: 0,
		error: "",
		requestCode: vi.fn().mockResolvedValue(true),
		confirmCode: vi.fn().mockResolvedValue(true),
		reset: vi.fn(),
		dispose: vi.fn()
	}
}));

describe("ProfileScreen", () => {
	beforeEach(() => {
		vi.clearAllMocks();
		vi.mocked(storeAuth).isLoggedIn = true;
		vi.mocked(storeAuth).emailVerified = false;
		(storeStats as { myStats: unknown }).myStats = { ...defaultStats };
		(storeStats as { isLoading: boolean }).isLoading = false;
	});

	it("shows the player's username and a stat summary", () => {
		render(ProfileScreen);
		expect(screen.getByText("eldyn")).toBeInTheDocument();
		expect(screen.getByText(/12/)).toBeInTheDocument();
	});

	it("offers a Leaderboard entry point and no Settings button", async () => {
		render(ProfileScreen);

		const leaderboard = screen.getByRole("button", { name: "Leaderboard" });
		expect(leaderboard).toBeInTheDocument();
		expect(screen.queryByRole("button", { name: /settings/i })).not.toBeInTheDocument();

		await fireEvent.click(leaderboard);
		expect(storeNavigation.goto).toHaveBeenCalledWith("stats");
	});

	it("fetches stats on mount when they haven't loaded yet", () => {
		(storeStats as { myStats: unknown }).myStats = null;
		render(ProfileScreen);
		expect(fetchMe).toHaveBeenCalled();
		expect(fetchMine).toHaveBeenCalled();
	});

	it("guest does not call fetchMe and shows the register call to action", async () => {
		vi.mocked(storeAuth).isLoggedIn = false;
		(storeStats as { myStats: unknown }).myStats = null;

		render(ProfileScreen);

		expect(fetchMe).not.toHaveBeenCalled();
		expect(fetchMine).not.toHaveBeenCalled();
		expect(screen.getByText(/register to track stats/i)).toBeInTheDocument();

		await fireEvent.click(screen.getByRole("button", { name: "Register" }));
		expect(storeNavigation.gotoAuth).toHaveBeenCalledWith("register");
	});

	it("shows the error state and Retry calls fetchMe again", async () => {
		(storeStats as { myStats: unknown }).myStats = null;

		render(ProfileScreen);

		await vi.waitFor(() => {
			expect(screen.getByText("Could not load stats.")).toBeInTheDocument();
		});
		expect(fetchMe).toHaveBeenCalledTimes(1);

		await fireEvent.click(screen.getByRole("button", { name: "Retry" }));
		expect(fetchMe).toHaveBeenCalledTimes(2);
	});

	it("renders Logout in the account header", () => {
		render(ProfileScreen);

		const logout = screen.getByRole("button", { name: /logout/i });
		const accountHeader = screen.getByText("Account").closest(".profile-header");
		expect(accountHeader).not.toBeNull();
		expect(accountHeader as HTMLElement).toContainElement(logout);
	});
});
