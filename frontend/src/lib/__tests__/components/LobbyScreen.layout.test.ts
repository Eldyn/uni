import { describe, it, expect, vi } from "vitest";
import { render, screen, fireEvent } from "@testing-library/svelte";

const { leave } = vi.hoisted(() => ({
	leave: vi.fn()
}));

vi.mock("$stores/auth.svelte", () => ({ storeAuth: { username: "eldyn" } }));
vi.mock("$stores/lobby.svelte", () => ({
	storeLobby: {
		current: {
			invite_code: "ABCD",
			host: "eldyn",
			name: "eldyn's game",
			members: [
				{ username: "eldyn", is_connected: true, is_host: true, is_bot: false, is_ready: false }
			],
			settings: { max_players: 4, bot_count: 0 }
		},
		startEligibility: { canStart: false, reason: "Waiting on players" },
		toggleReady: vi.fn(),
		updateSettings: vi.fn(),
		startMatch: vi.fn(),
		leave,
		isLoadingStart: false
	}
}));

import LobbyScreen from "$components/lobby/LobbyScreen.svelte";

describe("LobbyScreen responsive action bar placement", () => {
	it("renders settings and exit actions in top secondary header row, leaving bottom bar clear for chat", async () => {
		const { container } = render(LobbyScreen);
		const header = container.querySelector("header.shell-topbar-secondary");
		expect(header).toBeInTheDocument();

		const settingsBtn = screen.getByTitle(/settings/i);
		const exitBtn = screen.getByTitle(/exit/i);

		expect(header).toContainElement(settingsBtn);
		expect(header).toContainElement(exitBtn);

		// Assert no bottom bar exists outside scrollable container
		const bottomBars = container.querySelectorAll(".bottom-bar, footer");
		expect(bottomBars.length).toBe(0);

		// Verify leave triggers storeLobby.leave
		await fireEvent.click(exitBtn);
		expect(leave).toHaveBeenCalled();
	});

	it("renders a prominent pixel-bordered chip for the join code with reveal and copy actions", async () => {
		render(LobbyScreen);
		// Initially masked
		expect(screen.getByText("••••••")).toBeInTheDocument();

		const showCodeBtn = screen.getByRole("button", { name: /show code/i });
		expect(showCodeBtn).toBeInTheDocument();

		const copyBtn = screen.getByRole("button", { name: /copy invite link/i });
		expect(copyBtn).toBeInTheDocument();

		// Reveal code
		await fireEvent.click(showCodeBtn);
		expect(screen.getByText("ABCD")).toBeInTheDocument();

		const hideCodeBtn = screen.getByRole("button", { name: /hide code/i });
		expect(hideCodeBtn).toBeInTheDocument();
	});
});
