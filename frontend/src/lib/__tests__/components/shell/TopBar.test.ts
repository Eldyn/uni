import { beforeEach, describe, it, expect, vi } from "vitest";
import { render, screen } from "@testing-library/svelte";

const lobbyState = { current: null as { invite_code: string; members: unknown[] } | null };

const { balanceState } = vi.hoisted(() => ({
	balanceState: { balance: 1500 as number | undefined }
}));

const { avatarState } = vi.hoisted(() => ({
	avatarState: { loadout: null as { shape: string; skin: string; slots: object } | null }
}));

vi.mock("$lib/stores/navigation.svelte", () => ({
	storeNavigation: { current: "lobbies", goto: vi.fn(() => true) }
}));
vi.mock("$lib/stores/auth.svelte", () => ({
	storeAuth: { username: "eldyn", avatar: "", isGuest: false }
}));
vi.mock("$lib/stores/shop.svelte", () => ({
	storeShop: {
		get balance() {
			return balanceState.balance;
		}
	}
}));
vi.mock("$lib/stores/avatar.svelte", () => ({ storeAvatar: avatarState }));
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

// The real Avatar bakes through this helper; the fake records the last loadout
// it was handed and always exposes a sprite URL.
const baked = vi.hoisted(() => ({
	loadout: null as { shape: string; skin: string; slots: object } | null,
	set: vi.fn(),
	destroy: vi.fn()
}));
vi.mock("$lib/avatar/useBakedAvatar.svelte", () => ({
	createBakedAvatar: () => ({
		get url() {
			return baked.loadout ? `blob:${baked.loadout.shape}/${baked.loadout.skin}` : "blob:default";
		},
		set(loadout: { shape: string; skin: string; slots: object } | null) {
			baked.loadout = loadout;
			baked.set(loadout);
		},
		destroy: baked.destroy
	})
}));

import TopBar from "$components/shell/TopBar.svelte";
import { storeAuth } from "$stores/auth.svelte";

describe("TopBar", () => {
	beforeEach(() => {
		lobbyState.current = null;
		avatarState.loadout = null;
		baked.loadout = null;
		baked.set.mockClear();
		baked.destroy.mockClear();
		balanceState.balance = 1500;
		storeAuth.isGuest = false;
	});

	it("shows the username", () => {
		render(TopBar);
		expect(screen.getByText("eldyn")).toBeInTheDocument();
	});

	it("shows the Blank balance chip at the far right, after the identity button", () => {
		const { container } = render(TopBar);

		const right = container.querySelector(".shell-topbar-right");
		const chip = right?.querySelector<HTMLElement>("[data-balance]");
		const identity = right?.querySelector(".shell-topbar-identity");

		expect(chip).toBeInTheDocument();
		expect(chip?.getAttribute("data-balance")).toBe("1500");
		expect(chip?.querySelector('.blank-icon[role="img"]')?.getAttribute("aria-label")).toBe(
			"Blank"
		);
		expect(right?.lastElementChild).toBe(chip);
		expect(identity).not.toBeNull();
		expect(
			identity!.compareDocumentPosition(chip!) & Node.DOCUMENT_POSITION_FOLLOWING
		).toBeTruthy();
	});

	it("hides the balance chip for a guest", () => {
		storeAuth.isGuest = true;

		const { container } = render(TopBar);

		expect(container.querySelector("[data-balance]")).toBeNull();
	});

	it("hides the balance chip when the balance is unknown", () => {
		balanceState.balance = undefined;

		const { container } = render(TopBar);

		expect(container.querySelector("[data-balance]")).toBeNull();
	});

	it("bakes the identity avatar from the saved loadout", () => {
		const savedLook = {
			shape: "uni:round",
			skin: "sunset_4",
			slots: { eyes: { item: "uni:eyes_wide", colours: [] } }
		};
		avatarState.loadout = savedLook;

		const { container } = render(TopBar);

		expect(baked.set).toHaveBeenCalledWith(savedLook);
		expect(container.querySelector(".shell-topbar-identity .avatar-sprite")).toBeInTheDocument();
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

	it("shows a green ready check in the chip when the local player is ready", () => {
		lobbyState.current = {
			invite_code: "ABCD",
			members: [
				{ username: "eldyn", is_connected: true, is_host: true, is_bot: false, is_ready: true },
				{ username: "rival", is_connected: true, is_host: false, is_bot: false, is_ready: false }
			]
		};
		const { container } = render(TopBar);
		const glyph = container.querySelector(".pixelart-icons-font-check");
		expect(glyph).toBeInTheDocument();
		expect(glyph).toHaveClass("text-success");
	});

	it("shows a yellow clock in the chip when the local player is not ready", () => {
		lobbyState.current = {
			invite_code: "ABCD",
			members: [
				{ username: "eldyn", is_connected: true, is_host: true, is_bot: false, is_ready: false },
				{ username: "rival", is_connected: true, is_host: false, is_bot: false, is_ready: true }
			]
		};
		const { container } = render(TopBar);
		const glyph = container.querySelector(".pixelart-icons-font-clock");
		expect(glyph).toBeInTheDocument();
		expect(glyph).toHaveClass("text-gold");
	});
});
