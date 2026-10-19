// The Wardrobe screen: owned items per slot, the live draft on the stage, the
// blocked-by-set state, save and revert, the unsaved guard and the guest view.
// The economy, auth and avatar stores are mocked; WebGL is absent, so the real
// AvatarStage renders its fallback Avatar.

import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { cleanup, fireEvent, render, screen, within } from "@testing-library/svelte";
import { flushSync } from "svelte";
import MockThrelte from "$lib/__tests__/components/MockThrelte.svelte";
import WardrobeScreen from "$components/wardrobe/WardrobeScreen.svelte";
import { storeTopbarContent } from "$lib/stores/topbarContent.svelte";
import { setCatalogueForTest } from "$lib/avatar/catalogue";
import { avatarFixture, fullLoadout } from "$lib/__tests__/avatar/avatarFixture";
import type { Loadout } from "$lib/avatar/types";

vi.mock("@threlte/core", () => ({
	Canvas: MockThrelte,
	T: new Proxy(
		{},
		{
			get: () => MockThrelte
		}
	)
}));

const STARTERS = ["uni:round", "uni:eyes_dot", "uni:mouth_smile"];

const { shopMock } = vi.hoisted(() => ({
	shopMock: {
		balance: 1500,
		owned: new Set<string>(),
		buy: vi.fn()
	}
}));

vi.mock("$lib/stores/shopMock.svelte", () => ({ shopMock }));

const { storeAuth } = vi.hoisted(() => ({
	storeAuth: { isGuest: false, isLoggedIn: true }
}));

vi.mock("$stores/auth.svelte", () => ({ storeAuth }));

const { storeAvatar } = vi.hoisted(() => ({
	storeAvatar: {
		loadout: null as Loadout | null,
		save: vi.fn()
	}
}));

vi.mock("$stores/avatar.svelte", () => ({ storeAvatar }));

vi.mock("$lib/avatar/useBakedAvatar.svelte", () => ({
	createBakedAvatar: () => ({ url: null, set: vi.fn(), destroy: vi.fn() })
}));

function itemIds(container: HTMLElement): string[] {
	return [...container.querySelectorAll<HTMLElement>("[data-item-id]")].map(
		(card) => card.dataset.itemId ?? ""
	);
}

function cardFor(container: HTMLElement, id: string): HTMLElement {
	const card = container.querySelector<HTMLElement>(`[data-item-id="${id}"]`);
	if (!card) throw new Error(`no card for ${id}`);
	return card;
}

function equipped(container: HTMLElement): string | null {
	return container.querySelector("[data-equipped]")?.getAttribute("data-equipped") ?? null;
}

beforeEach(() => {
	vi.spyOn(HTMLCanvasElement.prototype, "getContext").mockReturnValue(null);
	setCatalogueForTest(avatarFixture());
	shopMock.owned = new Set(STARTERS);
	shopMock.buy.mockReset();
	storeAuth.isGuest = false;
	storeAuth.isLoggedIn = true;
	storeAvatar.loadout = null;
	storeAvatar.save.mockReset();
	storeAvatar.save.mockResolvedValue({ ok: true });
	storeTopbarContent.current = undefined;
});

afterEach(() => {
	cleanup();
	vi.restoreAllMocks();
});

describe("WardrobeScreen", () => {
	it("hands its title to the top bar while mounted", () => {
		const { unmount } = render(WardrobeScreen);
		flushSync();
		expect(storeTopbarContent.current).toBeTruthy();

		unmount();
		expect(storeTopbarContent.current).toBeUndefined();
	});

	it("lists the account's owned items per slot plus the starters", async () => {
		shopMock.owned = new Set(["uni:hat_cap"]);
		const { container } = render(WardrobeScreen);
		flushSync();

		expect(itemIds(container)).toEqual(["uni:round"]);

		await fireEvent.click(screen.getByRole("tab", { name: "Hat" }));
		expect(itemIds(container)).toEqual(["uni:hat_cap"]);

		await fireEvent.click(screen.getByRole("tab", { name: "Eyes" }));
		expect(itemIds(container)).toEqual(["uni:eyes_dot"]);
	});

	it("updates the stage when a card is selected", async () => {
		shopMock.owned = new Set([...STARTERS, "uni:hat_cap"]);
		const { container } = render(WardrobeScreen);
		flushSync();

		await fireEvent.click(screen.getByRole("tab", { name: "Hat" }));
		await fireEvent.click(cardFor(container, "uni:hat_cap"));

		expect(equipped(container)).toBe("uni:hat_cap");
	});

	it("blocks the part tabs a worn set covers and disables their cards", async () => {
		shopMock.owned = new Set([...STARTERS, "uni:hat_cap", "uni:set_suit"]);
		const { container } = render(WardrobeScreen);
		flushSync();

		await fireEvent.click(screen.getByRole("tab", { name: "Set" }));
		await fireEvent.click(cardFor(container, "uni:set_suit"));

		await fireEvent.click(screen.getByRole("tab", { name: "Hat" }));
		expect(screen.getByText(/blocked by set/i)).toBeInTheDocument();
		expect(cardFor(container, "uni:hat_cap")).toBeDisabled();
	});

	it("styles SAVE as the primary button and Revert as the secondary button", () => {
		render(WardrobeScreen);
		flushSync();
		expect(screen.getByRole("button", { name: "SAVE" })).toHaveClass("btn");
		expect(screen.getByRole("button", { name: "Revert" })).toHaveClass("btn-secondary");
	});

	it("saves the draft through storeAvatar", async () => {
		shopMock.owned = new Set([...STARTERS, "uni:hat_cap"]);
		const { container } = render(WardrobeScreen);
		flushSync();

		await fireEvent.click(screen.getByRole("tab", { name: "Hat" }));
		await fireEvent.click(cardFor(container, "uni:hat_cap"));
		await fireEvent.click(screen.getByRole("button", { name: "SAVE" }));

		await vi.waitFor(() => expect(storeAvatar.save).toHaveBeenCalledTimes(1));
		const saved = storeAvatar.save.mock.calls[0][0] as Loadout;
		expect(saved.slots.head_top?.item).toBe("uni:hat_cap");
	});

	it("restores the saved loadout on revert", async () => {
		shopMock.owned = new Set([...STARTERS, "uni:eyes_wide"]);
		storeAvatar.loadout = fullLoadout();
		const { container } = render(WardrobeScreen);
		flushSync();

		await fireEvent.click(screen.getByRole("tab", { name: "Eyes" }));
		await fireEvent.click(cardFor(container, "uni:eyes_wide"));
		expect(equipped(container)).toBe("uni:eyes_wide");

		await fireEvent.click(screen.getByRole("button", { name: "Revert" }));
		expect(equipped(container)).toBe("uni:eyes_dot");
	});

	it("shows the unsaved marker and asks before leaving", async () => {
		shopMock.owned = new Set([...STARTERS, "uni:hat_cap"]);
		const onleave = vi.fn();
		const { container } = render(WardrobeScreen, { props: { onleave } });
		flushSync();

		await fireEvent.click(screen.getByRole("tab", { name: "Hat" }));
		await fireEvent.click(cardFor(container, "uni:hat_cap"));
		expect(screen.getByText(/unsaved/i)).toBeInTheDocument();

		await fireEvent.click(screen.getByRole("button", { name: "Close" }));
		expect(onleave).not.toHaveBeenCalled();

		const dialog = screen.getByRole("dialog");
		await fireEvent.click(within(dialog).getByRole("button", { name: /leave/i }));
		expect(onleave).toHaveBeenCalledTimes(1);
	});

	it("leaves straight away when there are no unsaved changes", async () => {
		const onleave = vi.fn();
		render(WardrobeScreen, { props: { onleave } });
		flushSync();

		await fireEvent.click(screen.getByRole("button", { name: "Close" }));
		expect(onleave).toHaveBeenCalledTimes(1);
		expect(screen.queryByRole("dialog")).toBeNull();
	});

	it("shows a register prompt and no stage for a guest", () => {
		storeAuth.isGuest = true;
		const { container } = render(WardrobeScreen);
		flushSync();

		expect(screen.getByText(/register to customise/i)).toBeInTheDocument();
		expect(container.querySelector(".avatar-stage")).toBeNull();
	});
});
