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

export type AcceleratorId = "home" | "browse" | "decks" | "shop" | "profile" | "chat" | "menu";

/**
 * Accelerator key per destination. The sidebar destinations (home/browse/
 * decks/shop/menu) use plain digits so the shortcut is locale-invariant and
 * doesn't depend on which letter happens to appear in the localized label.
 * "profile" and "chat" are icon-only / avatar entry points with no visible
 * label, so they keep a mnemonic letter.
 */
const ACCELERATOR_KEYS: Record<AcceleratorId, string> = {
	home: "1",
	browse: "2",
	decks: "3",
	shop: "4",
	menu: "5",
	profile: "P",
	chat: "C"
};

/** Returns the accelerator key for `id`. */
export function acceleratorKey(id: AcceleratorId): string {
	return ACCELERATOR_KEYS[id];
}

const SCREEN_BY_ACCELERATOR: Partial<Record<AcceleratorId, AppScreen>> = {
	home: "main",
	browse: "lobbies",
	decks: "decks",
	shop: "shop",
	profile: "profile",
	menu: "settings"
};

/** Resolves a pressed (lowercased) key to a screen. */
function screenForKey(key: string): AppScreen | undefined {
	for (const id of Object.keys(SCREEN_BY_ACCELERATOR) as AcceleratorId[]) {
		if (ACCELERATOR_KEYS[id].toLowerCase() === key) return SCREEN_BY_ACCELERATOR[id];
	}
	return undefined;
}

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

	// A game prompt ("pick a color", "play it", "select a target") owns the
	// keyboard while it is open. Firing a screen shortcut here would yank the
	// player out of the match mid-answer; Escape above stays available.
	if (storeGame.actionRequired !== null) return;

	const key = event.key.toLowerCase();

	if (key === "c") {
		chatStore.open();
		return;
	}

	const screen = screenForKey(key);
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
