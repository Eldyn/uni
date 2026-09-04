import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";

const { gotoMock, openSettingsMock, chatOpenMock } = vi.hoisted(() => ({
	gotoMock: vi.fn(),
	openSettingsMock: vi.fn(),
	chatOpenMock: vi.fn()
}));

vi.mock("$stores/navigation.svelte", () => ({
	storeNavigation: { goto: gotoMock, openSettings: openSettingsMock }
}));
vi.mock("$stores/modal.svelte", () => ({ storeModal: { isAnyOpen: false } }));
vi.mock("$stores/lobby.svelte", () => ({ storeLobby: { isInLobby: false } }));
vi.mock("$stores/game.svelte", () => ({ storeGame: { state: null } }));
vi.mock("$stores/chat.svelte", () => ({ chatStore: { open: chatOpenMock } }));

import { initKeyboardAccelerators } from "$lib/actions/keyboardAccelerators";
import { storeModal } from "$stores/modal.svelte";
import { storeLobby } from "$stores/lobby.svelte";
import { storeGame } from "$stores/game.svelte";

function press(key: string, target: EventTarget = document.body) {
	const event = new KeyboardEvent("keydown", { key, bubbles: true });
	Object.defineProperty(event, "target", { value: target });
	document.dispatchEvent(event);
}

describe("keyboard accelerators", () => {
	let cleanup: () => void;

	beforeEach(() => {
		vi.clearAllMocks();
		storeModal.isAnyOpen = false;
		storeLobby.isInLobby = false;
		storeGame.state = null;
		cleanup = initKeyboardAccelerators();
	});

	afterEach(() => {
		cleanup();
	});

	it("H navigates home", () => {
		press("h");
		expect(gotoMock).toHaveBeenCalledWith("main");
	});

	it("B navigates to browse", () => {
		press("b");
		expect(gotoMock).toHaveBeenCalledWith("lobbies");
	});

	it("M navigates to the settings screen", () => {
		press("m");
		expect(gotoMock).toHaveBeenCalledWith("settings");
	});

	it("C opens chat", () => {
		press("c");
		expect(chatOpenMock).toHaveBeenCalled();
	});

	it("Escape opens the settings modal while in a lobby", () => {
		storeLobby.isInLobby = true;
		press("Escape");
		expect(openSettingsMock).toHaveBeenCalled();
	});

	it("Escape does nothing outside a lobby or match", () => {
		press("Escape");
		expect(openSettingsMock).not.toHaveBeenCalled();
	});

	it("ignores keys while a modal is open", () => {
		storeModal.isAnyOpen = true;
		press("h");
		expect(gotoMock).not.toHaveBeenCalled();
	});

	it("ignores keys while focus is in a text input", () => {
		const input = document.createElement("input");
		document.body.appendChild(input);
		press("h", input);
		expect(gotoMock).not.toHaveBeenCalled();
		input.remove();
	});

	it("cleanup removes the listener", () => {
		cleanup();
		press("h");
		expect(gotoMock).not.toHaveBeenCalled();
	});
});
