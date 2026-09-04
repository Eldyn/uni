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
import { storeI18n } from "$stores/i18n.svelte";
import type { AppScreen } from "$stores/navigation.svelte";

export type AcceleratorId = "home" | "browse" | "decks" | "shop" | "profile" | "chat" | "menu";

/**
 * Mnemonic letter per destination, per locale. Each letter must actually
 * appear in that locale's nav label (see NavBar.svelte's mnemonicLabel
 * snippet, which underlines the matching letter) so the visible mnemonic and
 * the advertised `aria-keyshortcuts` stay truthful when the label changes
 * with the locale. "profile" and "chat" have no visible mnemonic label
 * (icon-only / avatar entry points), so their key is locale-invariant.
 */
const ACCELERATOR_KEYS_BY_LOCALE: Record<string, Record<AcceleratorId, string>> = {
	en: { home: "H", browse: "B", decks: "D", shop: "S", profile: "P", chat: "C", menu: "M" },
	// it labels: Home, Sfoglia, Mazzi, Negozio, Menu — H/S collide with nothing,
	// but Mazzi and Menu both start with M, so Decks/Menu borrow a later,
	// still-present letter (Z from "MaZzi", U from "MenU") to stay unique.
	it: { home: "H", browse: "S", decks: "Z", shop: "N", profile: "P", chat: "C", menu: "U" },
	// pseudo labels wrap the English word in brackets/diacritics but keep its
	// first real letter (e.g. "[Höme swéét höme]"), so the English keys still
	// each appear in their label.
	pseudo: { home: "H", browse: "B", decks: "D", shop: "S", profile: "P", chat: "C", menu: "M" }
};

/** Returns the accelerator letter for `id` in the given locale, falling back to English. */
export function acceleratorKey(id: AcceleratorId, locale: string): string {
	return (ACCELERATOR_KEYS_BY_LOCALE[locale] ?? ACCELERATOR_KEYS_BY_LOCALE.en)[id];
}

const SCREEN_BY_ACCELERATOR: Partial<Record<AcceleratorId, AppScreen>> = {
	home: "main",
	browse: "lobbies",
	decks: "decks",
	shop: "shop",
	profile: "profile",
	menu: "settings"
};

/** Resolves a pressed (lowercased) key to a screen, honoring the active locale's accelerator map. */
function screenForKey(key: string, locale: string): AppScreen | undefined {
	const map = ACCELERATOR_KEYS_BY_LOCALE[locale] ?? ACCELERATOR_KEYS_BY_LOCALE.en;
	for (const id of Object.keys(SCREEN_BY_ACCELERATOR) as AcceleratorId[]) {
		if (map[id].toLowerCase() === key) return SCREEN_BY_ACCELERATOR[id];
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

	const key = event.key.toLowerCase();

	if (key === "c") {
		chatStore.open();
		return;
	}

	const screen = screenForKey(key, storeI18n.locale);
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
