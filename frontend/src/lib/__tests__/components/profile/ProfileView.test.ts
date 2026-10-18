import { describe, it, expect, vi } from "vitest";
import { render, screen, fireEvent } from "@testing-library/svelte";
import { createRawSnippet } from "svelte";

import ProfileView from "$components/profile/ProfileView.svelte";
import type { PlayerStats } from "$stores/stats.svelte";

function makeStats(overrides: Partial<PlayerStats> = {}): PlayerStats {
	return {
		username: "eldyn",
		total_wins: 12,
		total_losses: 8,
		rank: 3,
		...overrides
	};
}

function makeProfile(stats: PlayerStats | null) {
	return { username: "eldyn", loadout: null, stats };
}

describe("ProfileView", () => {
	it("shows username, stats and win rate when ready", () => {
		render(ProfileView, {
			props: { profile: makeProfile(makeStats()), isOwn: true, status: "ready" }
		});

		expect(screen.getByText("eldyn")).toBeInTheDocument();
		expect(screen.getByText("12")).toBeInTheDocument();
		expect(screen.getByText("8")).toBeInTheDocument();
		expect(screen.getByText("60%")).toBeInTheDocument();
		expect(screen.getByText("20")).toBeInTheDocument();
		expect(screen.getByText("#3")).toBeInTheDocument();
	});

	it("shows Unranked for rank 0 and null", () => {
		const { unmount } = render(ProfileView, {
			props: {
				profile: makeProfile(makeStats({ rank: 0 })),
				isOwn: true,
				status: "ready"
			}
		});
		expect(screen.getByText("Unranked")).toBeInTheDocument();
		unmount();

		render(ProfileView, {
			props: {
				profile: makeProfile(makeStats({ rank: null })),
				isOwn: true,
				status: "ready"
			}
		});
		expect(screen.getByText("Unranked")).toBeInTheDocument();
	});

	it("shows an em dash for win rate with zero games", () => {
		render(ProfileView, {
			props: {
				profile: makeProfile(makeStats({ total_wins: 0, total_losses: 0, rank: null })),
				isOwn: true,
				status: "ready"
			}
		});

		expect(screen.getByText("—")).toBeInTheDocument();
		expect(screen.queryByText("NaN%")).not.toBeInTheDocument();
	});

	it("shows em dashes while loading", () => {
		render(ProfileView, {
			props: { profile: makeProfile(null), isOwn: true, status: "loading" }
		});

		expect(screen.getAllByText("—")).toHaveLength(5);
	});

	it("shows the error message and Retry calls onRetry", async () => {
		const onRetry = vi.fn();
		render(ProfileView, {
			props: { profile: makeProfile(null), isOwn: true, status: "error", onRetry }
		});

		expect(screen.getByText("Could not load stats.")).toBeInTheDocument();
		await fireEvent.click(screen.getByRole("button", { name: "Retry" }));
		expect(onRetry).toHaveBeenCalledTimes(1);
	});

	it("guest shows the call to action, hides Stats and Cards played, Register calls onRegister", async () => {
		const onRegister = vi.fn();
		render(ProfileView, {
			props: {
				profile: { username: "Guest#12345", loadout: null, stats: null },
				isOwn: false,
				status: "guest",
				onRegister
			}
		});

		expect(screen.getByText(/register to track stats/i)).toBeInTheDocument();
		expect(screen.queryByText("Stats")).not.toBeInTheDocument();
		expect(screen.queryByText("Cards played")).not.toBeInTheDocument();
		expect(screen.queryByRole("tablist")).not.toBeInTheDocument();

		await fireEvent.click(screen.getByRole("button", { name: "Register" }));
		expect(onRegister).toHaveBeenCalledTimes(1);
	});

	it("hides Cards played when every counter is zero", () => {
		render(ProfileView, {
			props: {
				profile: makeProfile(makeStats({ cards_played_red: 0, cards_played_blue: 0 })),
				isOwn: true,
				status: "ready"
			}
		});

		expect(screen.queryByText("Cards played")).not.toBeInTheDocument();
		expect(screen.queryByRole("tablist")).not.toBeInTheDocument();
	});

	it("shows the cards played chart when counters exist", () => {
		render(ProfileView, {
			props: {
				profile: makeProfile(makeStats({ cards_played_red: 4, cards_played_blue: 2 })),
				isOwn: true,
				status: "ready"
			}
		});

		expect(screen.getByText("Cards played")).toBeInTheDocument();
		expect(screen.getByRole("tablist")).toBeInTheDocument();
	});

	it("leaderboard button calls onLeaderboard", async () => {
		const onLeaderboard = vi.fn();
		render(ProfileView, {
			props: { profile: makeProfile(makeStats()), isOwn: true, status: "ready", onLeaderboard }
		});

		await fireEvent.click(screen.getByRole("button", { name: /leaderboard/i }));
		expect(onLeaderboard).toHaveBeenCalledTimes(1);
	});

	it("renders the account and accountAction snippets only when isOwn", () => {
		const account = createRawSnippet(() => ({
			render: () => `<p>Account body</p>`
		}));
		const accountAction = createRawSnippet(() => ({
			render: () => `<button type="button">Logout</button>`
		}));

		const { unmount } = render(ProfileView, {
			props: {
				profile: makeProfile(makeStats()),
				isOwn: true,
				status: "ready",
				account,
				accountAction
			}
		});
		expect(screen.getByText("Account body")).toBeInTheDocument();
		const logout = screen.getByRole("button", { name: "Logout" });
		const accountHeader = screen.getByText("Account").closest(".profile-header");
		expect(accountHeader).not.toBeNull();
		expect(accountHeader as HTMLElement).toContainElement(logout);
		unmount();

		render(ProfileView, {
			props: {
				profile: makeProfile(makeStats()),
				isOwn: false,
				status: "ready",
				account,
				accountAction
			}
		});
		expect(screen.queryByText("Account body")).not.toBeInTheDocument();
		expect(screen.queryByRole("button", { name: "Logout" })).not.toBeInTheDocument();
		expect(screen.queryByRole("button", { name: /verify/i })).not.toBeInTheDocument();
	});
});
