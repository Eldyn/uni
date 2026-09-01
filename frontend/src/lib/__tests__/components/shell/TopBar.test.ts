import { describe, it, expect, vi } from "vitest";
import { render, screen } from "@testing-library/svelte";

const lobbyState = { current: null as { invite_code: string; members: unknown[] } | null };

vi.mock("$lib/stores/navigation.svelte", () => ({
	storeNavigation: { current: "lobbies", goto: vi.fn(() => true) }
}));
vi.mock("$lib/stores/auth.svelte", () => ({
	storeAuth: { username: "eldyn", avatar: "" }
}));
vi.mock("$lib/stores/lobby.svelte", () => ({
	storeLobby: {
		get isInLobby() {
			return lobbyState.current !== null;
		},
		get current() {
			return lobbyState.current;
		}
	}
}));

import TopBar from "$components/shell/TopBar.svelte";

describe("TopBar", () => {
	it("shows the username", () => {
		render(TopBar);
		expect(screen.getByText("eldyn")).toBeInTheDocument();
	});

	it("hides the lobby chip when not in a lobby", () => {
		lobbyState.current = null;
		render(TopBar);
		expect(screen.queryByText(/lobby/i)).not.toBeInTheDocument();
	});

	it("shows the lobby chip with a ready count when in a lobby", () => {
		lobbyState.current = {
			invite_code: "ABCD",
			// Real LobbyMember shape (no `is_ready` field yet). Until it
			// exists, TopBar's readyCount filter reads `is_ready` as
			// undefined/falsy on every member, so the chip reads 0/2 rather
			// than the eventual 1/2.
			members: [
				{ username: "eldyn", is_connected: true, is_host: true, is_bot: false },
				{ username: "rival", is_connected: true, is_host: false, is_bot: false }
			]
		};
		render(TopBar);
		expect(screen.getByText(/0\s*\/\s*2/)).toBeInTheDocument();
	});
});
