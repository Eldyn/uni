// The Wardrobe screen: owned items per slot, the live draft on the stage, the
// blocked-by-set state, save and revert, the unsaved guard and the guest view.
// The economy and auth stores are mocked; the real avatar store is used with
// spied requests so a late loadout exercises its reactivity. WebGL is absent,
// so the real AvatarStage renders its fallback Avatar.

import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { cleanup, fireEvent, render, screen, within } from "@testing-library/svelte";
import { flushSync } from "svelte";
import MockThrelte from "$lib/__tests__/components/MockThrelte.svelte";
import WardrobeScreen from "$components/wardrobe/WardrobeScreen.svelte";
import { storeTopbarContent } from "$lib/stores/topbarContent.svelte";
import { setShopBackend } from "$lib/stores/shop.svelte";
import { setCatalogueForTest } from "$lib/avatar/catalogue";
import { avatarFixture, fullLoadout } from "$lib/__tests__/avatar/avatarFixture";
import type { Loadout } from "$lib/avatar/types";
import { storeNavigation } from "$stores/navigation.svelte";
import { storeAvatar } from "$stores/avatar.svelte";
import wardrobeSource from "../../components/wardrobe/WardrobeScreen.svelte?raw";

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
		buy: vi.fn(),
		fetchMine: vi.fn(),
		needsLogin: false
	}
}));

const { storeAuth } = vi.hoisted(() => ({
	storeAuth: { isGuest: false, isLoggedIn: true }
}));

vi.mock("$stores/auth.svelte", () => ({ storeAuth }));

const { storeNavigation } = vi.hoisted(() => ({
	storeNavigation: { goto: vi.fn(), setLeaveGuard: vi.fn() }
}));

vi.mock("$stores/navigation.svelte", () => ({ storeNavigation }));

const baked = vi.hoisted(() => ({ set: vi.fn(), destroy: vi.fn() }));

vi.mock("$lib/avatar/useBakedAvatar.svelte", () => ({
	createBakedAvatar: () => ({
		get url() {
			return "blob:wardrobe";
		},
		set: baked.set,
		destroy: baked.destroy
	})
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

function cssBlock(source: string, selector: string): string {
	const escaped = selector.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
	const match = source.match(new RegExp(`${escaped}\\s*\\{([^}]*)\\}`));
	if (!match) throw new Error(`rule not found: ${selector}`);
	return match[1];
}

function leaveGuard(): (to: string) => boolean {
	const calls = storeNavigation.setLeaveGuard.mock.calls;
	return [...calls].reverse().find((call) => call[0])?.[0];
}

beforeEach(() => {
	vi.spyOn(HTMLCanvasElement.prototype, "getContext").mockReturnValue(null);
	setCatalogueForTest(avatarFixture());
	setShopBackend(shopMock);
	shopMock.owned = new Set(STARTERS);
	shopMock.buy.mockReset();
	shopMock.fetchMine.mockReset();
	storeAuth.isGuest = false;
	storeAuth.isLoggedIn = true;
	storeAvatar.loadout = null;
	storeAvatar.status = "idle";
	vi.spyOn(storeAvatar, "save").mockResolvedValue({ ok: true });
	vi.spyOn(storeAvatar, "fetchMine").mockResolvedValue(undefined);
	storeTopbarContent.current = undefined;
	storeNavigation.goto.mockReset();
	storeNavigation.setLeaveGuard.mockReset();
});

afterEach(() => {
	cleanup();
	vi.restoreAllMocks();
});

describe("WardrobeScreen", () => {
	it("renders no heading of its own; the top bar shows the title", () => {
		render(WardrobeScreen);
		flushSync();

		expect(screen.queryByRole("heading", { level: 1 })).not.toBeInTheDocument();
		expect(storeTopbarContent.current).toBeUndefined();
	});

	it("shows Discard Changes only when the draft differs from the saved look", async () => {
		shopMock.owned = new Set([...STARTERS, "uni:hat_cap"]);
		const { container } = render(WardrobeScreen);
		flushSync();
		const controls = () =>
			[...(container.querySelector(".wardrobe-header")?.querySelectorAll("button") ?? [])].map(
				(button) => button.textContent?.trim()
			);
		expect(controls()).toEqual(["SAVE"]);
		expect(screen.queryByText(/unsaved/i)).toBeNull();

		await fireEvent.click(screen.getByRole("tab", { name: "Hat" }));
		await fireEvent.click(cardFor(container, "uni:hat_cap"));
		expect(controls()).toEqual(["Discard Changes", "SAVE"]);
		expect(screen.queryByText(/unsaved/i)).toBeNull();
		expect(cssBlock(wardrobeSource, ".wardrobe-header")).toMatch(/justify-content:\s*flex-end/);
	});

	it("adopts a late saved loadout without offering Discard Changes for it", async () => {
		shopMock.owned = new Set([...STARTERS, "uni:hat_cap"]);
		const savedLook = fullLoadout({ skin: "basic:green" });
		vi.mocked(storeAvatar.fetchMine).mockImplementation(async () => {
			storeAvatar.loadout = savedLook;
			storeAvatar.status = "ready";
		});

		const { container } = render(WardrobeScreen);
		flushSync();

		await vi.waitFor(() =>
			expect(container.querySelector('[data-swatch-id="basic:green"]')).toHaveAttribute(
				"aria-pressed",
				"true"
			)
		);
		expect(storeAvatar.fetchMine).toHaveBeenCalledTimes(1);
		expect(screen.queryByRole("button", { name: "Discard Changes" })).toBeNull();
		expect(screen.getByRole("button", { name: "SAVE" })).toBeInTheDocument();
	});

	it("bakes each item tile with default colors on the draft shape and skin", async () => {
		baked.set.mockClear();
		render(WardrobeScreen);
		flushSync();

		await fireEvent.click(screen.getByRole("tab", { name: "Eyes" }));

		const tileLoadouts = baked.set.mock.calls.map((call) => call[0]);
		expect(
			tileLoadouts.some(
				(loadout) =>
					loadout &&
					loadout.slots.eyes?.item === "uni:eyes_dot" &&
					loadout.slots.eyes.colours.length === 1
			)
		).toBe(true);
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

	it("styles SAVE as the primary button and Discard Changes as the secondary button", async () => {
		shopMock.owned = new Set([...STARTERS, "uni:hat_cap"]);
		const { container } = render(WardrobeScreen);
		flushSync();
		await fireEvent.click(screen.getByRole("tab", { name: "Hat" }));
		await fireEvent.click(cardFor(container, "uni:hat_cap"));

		expect(screen.getByRole("button", { name: "SAVE" })).toHaveClass("btn");
		expect(screen.getByRole("button", { name: "Discard Changes" })).toHaveClass("btn-secondary");
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

	it("shows the save error and refreshes ownership when the server rejects an unowned item", async () => {
		shopMock.owned = new Set([...STARTERS, "uni:hat_cap"]);
		storeAvatar.save.mockResolvedValue({ ok: false, code: "not_owned" });
		const { container } = render(WardrobeScreen);
		flushSync();
		shopMock.fetchMine.mockClear();

		await fireEvent.click(screen.getByRole("tab", { name: "Hat" }));
		await fireEvent.click(cardFor(container, "uni:hat_cap"));
		await fireEvent.click(screen.getByRole("button", { name: "SAVE" }));

		await vi.waitFor(() => expect(screen.getByRole("alert")).toHaveTextContent(/no longer yours/i));
		expect(shopMock.fetchMine).toHaveBeenCalledTimes(1);
	});

	it("restores the saved loadout on revert", async () => {
		shopMock.owned = new Set([...STARTERS, "uni:eyes_wide"]);
		storeAvatar.loadout = fullLoadout();
		const { container } = render(WardrobeScreen);
		flushSync();

		await fireEvent.click(screen.getByRole("tab", { name: "Eyes" }));
		await fireEvent.click(cardFor(container, "uni:eyes_wide"));
		expect(equipped(container)).toBe("uni:eyes_wide");

		await fireEvent.click(screen.getByRole("button", { name: "Discard Changes" }));
		expect(equipped(container)).toBe("uni:eyes_dot");
	});

	it("has no close button", () => {
		render(WardrobeScreen);
		flushSync();
		expect(screen.queryByRole("button", { name: "Close" })).toBeNull();
	});

	it("asks before it leaves to another screen with unsaved changes", async () => {
		shopMock.owned = new Set([...STARTERS, "uni:hat_cap"]);
		const { container } = render(WardrobeScreen);
		flushSync();
		await fireEvent.click(screen.getByRole("tab", { name: "Hat" }));
		await fireEvent.click(cardFor(container, "uni:hat_cap"));

		expect(leaveGuard()("shop")).toBe(false);
		flushSync();
		const dialog = screen.getByRole("dialog");
		await fireEvent.click(within(dialog).getByRole("button", { name: /^leave$/i }));
		expect(storeNavigation.goto).toHaveBeenCalledWith("shop");
		expect(leaveGuard()("shop")).toBe(true);
	});

	it("keeps editing when the user cancels the prompt", async () => {
		shopMock.owned = new Set([...STARTERS, "uni:hat_cap"]);
		const { container } = render(WardrobeScreen);
		flushSync();
		await fireEvent.click(screen.getByRole("tab", { name: "Hat" }));
		await fireEvent.click(cardFor(container, "uni:hat_cap"));

		leaveGuard()("shop");
		flushSync();
		await fireEvent.click(screen.getByRole("button", { name: /keep editing/i }));
		expect(storeNavigation.goto).not.toHaveBeenCalled();
		expect(screen.queryByRole("dialog")).toBeNull();
	});

	it("leaves at once without changes, never blocks a match, and clears the guard", async () => {
		const { unmount } = render(WardrobeScreen);
		flushSync();
		expect(leaveGuard()("shop")).toBe(true);
		expect(leaveGuard()("game")).toBe(true);
		unmount();
		expect(storeNavigation.setLeaveGuard).toHaveBeenLastCalledWith(null);
	});

	it("shows a register prompt and no stage for a guest", () => {
		storeAuth.isGuest = true;
		const { container } = render(WardrobeScreen);
		flushSync();

		expect(screen.getByText(/register to customise/i)).toBeInTheDocument();
		expect(container.querySelector(".avatar-stage")).toBeNull();
	});

	it("shows no color row for a worn item with no regions", async () => {
		shopMock.owned = new Set([...STARTERS, "uni:set_suit"]);
		const { container } = render(WardrobeScreen);
		flushSync();

		await fireEvent.click(screen.getByRole("tab", { name: "Set" }));
		await fireEvent.click(cardFor(container, "uni:set_suit"));

		// INFO: the only row group left is the skin row on the left.
		expect(container.querySelectorAll(".colour-rows")).toHaveLength(1);
	});

	it("shows a color row for a worn item with regions", async () => {
		const { container } = render(WardrobeScreen);
		flushSync();

		await fireEvent.click(screen.getByRole("tab", { name: "Eyes" }));
		await fireEvent.click(cardFor(container, "uni:eyes_dot"));

		expect(container.querySelectorAll(".colour-rows")).toHaveLength(2);
	});

	it("labels the skin row Skin so it does not repeat the item's Region 1 row", async () => {
		const { container } = render(WardrobeScreen);
		flushSync();

		await fireEvent.click(screen.getByRole("tab", { name: "Eyes" }));
		await fireEvent.click(cardFor(container, "uni:eyes_dot"));

		expect(container.querySelectorAll(".colour-rows")).toHaveLength(2);
		expect(screen.getAllByText("Skin")).toHaveLength(1);
		expect(screen.getAllByText("Region 1")).toHaveLength(1);
	});

	it("marks a locked paid swatch and opens the Shop when it is clicked", async () => {
		const { container } = render(WardrobeScreen);
		flushSync();

		await fireEvent.click(container.querySelector('[data-palette-id="uni:neon"]')!);

		const locked = container.querySelector<HTMLElement>('[data-swatch-id="uni:neon:hot"]');
		expect(locked).not.toBeNull();
		expect(locked?.dataset.locked).toBe("true");
		expect(locked?.querySelector(".pixelart-icons-font-lock")).toBeNull();

		await fireEvent.click(locked!);
		expect(storeNavigation.goto).toHaveBeenCalledWith("shop");
	});

	it("opens on the palette of the worn color and strikes the locked palettes", () => {
		const { container } = render(WardrobeScreen);
		flushSync();

		expect(container.querySelector('[data-swatch-id="basic:red"]')).not.toBeNull();
		expect(container.querySelector('[data-swatch-id="uni:neon:hot"]')).toBeNull();
		expect(
			container.querySelector<HTMLElement>('[data-palette-id="uni:neon"]')?.dataset.locked
		).toBe("true");
	});

	it("shows no color picker for an item with the none tint", async () => {
		const catalogue = avatarFixture();
		const hat = catalogue.items.find((entry) => entry.id === "uni:hat_cap");
		if (hat) hat.tint = "none";
		setCatalogueForTest(catalogue);
		shopMock.owned = new Set([...STARTERS, "uni:hat_cap"]);
		const { container } = render(WardrobeScreen);
		flushSync();
		await fireEvent.click(screen.getByRole("tab", { name: "Hat" }));
		await fireEvent.click(cardFor(container, "uni:hat_cap"));

		expect(screen.queryByRole("group", { name: "Colours" })).toBeNull();
	});

	it("gives the stage the main space of the screen", () => {
		expect(cssBlock(wardrobeSource, ".wardrobe-body")).toMatch(
			/grid-template-columns:\s*minmax\(0, 1\.2fr\) minmax\(0, 1fr\)/
		);
		expect(cssBlock(wardrobeSource, ".wardrobe-stage")).toMatch(/min-height:\s*384px/);
	});
});
