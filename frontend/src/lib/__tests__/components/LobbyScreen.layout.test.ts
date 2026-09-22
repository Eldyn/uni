import { describe, it, expect, vi } from "vitest";
import { render, screen, fireEvent } from "@testing-library/svelte";

const { leave } = vi.hoisted(() => ({
	leave: vi.fn()
}));

vi.mock("$stores/auth.svelte", () => ({ storeAuth: { username: "eldyn" } }));
vi.mock("$stores/chat.svelte", () => ({
	chatStore: { friends: [], selectChannel: vi.fn(), open: vi.fn() }
}));
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
	it("keeps settings and exit in the secondary header on desktop and pins START outside the scroll region", async () => {
		const { container } = render(LobbyScreen);
		const header = container.querySelector("header.shell-topbar-secondary");
		expect(header).toBeInTheDocument();

		const settingsBtn = screen.getByTitle(/settings/i);
		const exitBtn = screen.getByTitle(/exit/i);

		expect(header).toContainElement(settingsBtn);
		expect(header).toContainElement(exitBtn);

		// The START action bar sits outside the seat scroll area, so it can
		// never be scrolled away from regardless of player count.
		const scrollRegion = container.querySelector(".overflow-y-auto");
		const actionBar = container.querySelector("footer");
		expect(scrollRegion).not.toBeNull();
		expect(actionBar).toBeInTheDocument();
		expect(scrollRegion!.contains(actionBar!)).toBe(false);

		// Verify leave triggers storeLobby.leave
		await fireEvent.click(exitBtn);
		expect(leave).toHaveBeenCalled();
	});

	it("renders a prominent pixel-bordered chip for the join code with reveal and copy actions", async () => {
		const { container } = render(LobbyScreen);
		// The code is rendered one fixed-width cell per character, so its text
		// is split across spans — assert on the chip's own text content.
		const chip = container.querySelector(".invite-code")!;
		expect(chip).toBeInTheDocument();
		const codeText = () => chip.textContent?.replace(/\s+/g, "") ?? "";

		// Initially masked
		expect(codeText()).toBe("••••••");

		const showCodeBtn = screen.getByRole("button", { name: /show code/i });
		expect(showCodeBtn).toBeInTheDocument();

		const copyBtn = screen.getByRole("button", { name: /copy invite link/i });
		expect(copyBtn).toBeInTheDocument();

		// Reveal code
		await fireEvent.click(showCodeBtn);
		expect(codeText()).toBe("ABCD");

		const hideCodeBtn = screen.getByRole("button", { name: /hide code/i });
		expect(hideCodeBtn).toBeInTheDocument();
	});
});
