/**
 * @file keyboardAccelerators.ts
 * @brief Desktop single-key navigation accelerators.
 * Active only when focus is outside a text input and no modal is open.
 */
import { storeNavigation } from "$stores/navigation.svelte";
import { storeModal } from "$stores/modal.svelte";
import { storeLobby } from "$stores/lobby.svelte";
import { storeGame } from "$stores/game.svelte";
import { chatStore } from "$stores/chat.svelte";
import type { AppScreen } from "$stores/navigation.svelte";

/** Mnemonic letter per destination. English only for now — see localizeAccelerators() below for the per-locale hook that wires into the message catalogue. */
export const ACCELERATOR_KEYS: Record<
	"home" | "browse" | "decks" | "shop" | "profile" | "chat" | "menu",
	string
> = {
	home: "H",
	browse: "B",
	decks: "D",
	shop: "S",
	profile: "P",
	chat: "C",
	menu: "M"
};

const SCREEN_FOR_KEY: Record<string, AppScreen> = {
	h: "main",
	b: "lobbies",
	d: "decks",
	s: "shop",
	p: "profile",
	m: "settings"
};

const TEXT_INPUT_TAGS = new Set(["INPUT", "TEXTAREA", "SELECT"]);

function isTextInput(target: EventTarget | null): boolean {
	if (!(target instanceof HTMLElement)) return false;
	if (TEXT_INPUT_TAGS.has(target.tagName)) return true;
	return target.isContentEditable;
}

function onKeydown(event: KeyboardEvent): void {
	if (storeModal.isAnyOpen) return;
	if (isTextInput(event.target)) return;
	if (event.metaKey || event.ctrlKey || event.altKey) return;

	if (event.key === "Escape") {
		if (storeLobby.isInLobby || storeGame.state !== null) {
			storeNavigation.openSettings();
		}
		return;
	}

	const key = event.key.toLowerCase();

	if (key === "c") {
		chatStore.open();
		return;
	}

	const screen = SCREEN_FOR_KEY[key];
	if (screen) {
		storeNavigation.goto(screen);
	}
}

/**
 * @brief Installs the document-level keydown listener. Call once from a
 * persistent mount point (ShellFrame.svelte) and invoke the returned
 * cleanup on teardown.
 */
export function initKeyboardAccelerators(): () => void {
	document.addEventListener("keydown", onKeydown);
	return () => document.removeEventListener("keydown", onKeydown);
}
