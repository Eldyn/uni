/**
 * @file lobbyBrowse.ts
 * @brief Pure view-model helpers for the lobby browse screen: mapping,
 * filtering, sorting and per-card join affordances. No Svelte state, unit
 * testable in isolation.
 */

import type { SortKey } from "$lib/data/lobbyCatalogs";
import type { ListedLobby } from "$lib/stores/lobby.svelte";
import { locales } from "$lib/paraglide/runtime.js";
import * as m from "$lib/paraglide/messages.js";

type Locale = (typeof locales)[number];

export interface BrowseLobby {
	invite_code: string;
	name: string;
	status: "open" | "in-game" | "full";
	humans: number;
	bots: number;
	max: number;
	deck: string;
	allowBotTakeover: boolean;
	rules: string[];
}

export interface BrowseFilters {
	nameQuery: string;
	quickOpenOnly: boolean;
	quickHideInGame: boolean;
	status: { open: boolean; inGame: boolean; full: boolean };
	minOpenSlots: number;
	takeoverOnly: boolean;
	/** Deck names that must match (empty = any). */
	decks: string[];
	/** Rule ids that must all be active (empty = any). */
	rules: string[];
}

/** Join-button descriptor; `label: null` renders no action button at all. */
export interface JoinInfo {
	dot: string;
	label: string | null;
	bg: string;
	disabled: boolean;
	title: string;
}

export function toBrowseLobby(l: ListedLobby): BrowseLobby {
	return {
		invite_code: l.invite_code,
		name: l.name,
		status: l.status,
		humans: l.member_count || 0,
		bots: l.bot_count || 0,
		max: l.max_players || 4,
		deck: "Default", // TODO: no backend deck concept yet, stays mocked
		allowBotTakeover: l.allow_bot_takeover,
		rules: l.active_mods ?? []
	};
}

export const filled = (l: BrowseLobby): number => l.humans + l.bots;

export const openSlots = (l: BrowseLobby): number => Math.max(0, l.max - filled(l));

export function category(l: BrowseLobby): "open" | "inGame" | "full" {
	if (l.status === "in-game") return "inGame";
	if (l.status === "full") return "full";
	return "open";
}

export function joinInfo(l: BrowseLobby, locale: Locale): JoinInfo {
	// A bot occupying a slot can be overtaken by a joiner; without that, a
	// lobby at capacity is unjoinable regardless of its reported status.
	const canOvertake = l.allowBotTakeover && l.bots > 0;
	const atCapacity = filled(l) >= l.max;

	if (l.status === "in-game") {
		if (canOvertake)
			return {
				dot: "bg-orange-400",
				label: m.browse_join_label({}, { locale }),
				bg: "bg-orange-500",
				disabled: false,
				title: m.browse_join_title_takeover({}, { locale })
			};
		return {
			dot: "bg-red-500",
			label: null,
			bg: "",
			disabled: true,
			title: m.browse_join_title_ingame({}, { locale })
		};
	}
	if (l.status === "full" || (atCapacity && !canOvertake))
		return {
			dot: "bg-zinc-500",
			label: m.browse_full_label({}, { locale }),
			bg: "bg-surface-2",
			disabled: true,
			title: m.browse_join_title_full({}, { locale })
		};
	if (atCapacity && canOvertake)
		return {
			dot: "bg-orange-400",
			label: m.browse_join_label({}, { locale }),
			bg: "bg-orange-500",
			disabled: false,
			title: m.browse_join_title_takeover({}, { locale })
		};
	return {
		dot: "bg-green-500",
		label: m.browse_play_label({}, { locale }),
		bg: "bg-accent",
		disabled: false,
		title: m.browse_join_title_open({}, { locale })
	};
}

export function filterLobbies(lobbies: BrowseLobby[], f: BrowseFilters): BrowseLobby[] {
	const q = f.nameQuery.trim().toLowerCase();
	return lobbies.filter((l) => {
		if (q && !l.name.toLowerCase().includes(q)) return false;
		if (f.quickHideInGame && l.status === "in-game") return false;
		if (f.quickOpenOnly && (l.status !== "open" || openSlots(l) === 0)) return false;

		if (!f.status[category(l)]) return false;
		if (openSlots(l) < f.minOpenSlots) return false;
		if (f.takeoverOnly && !l.allowBotTakeover) return false;
		if (f.decks.length && !f.decks.includes(l.deck)) return false;
		if (f.rules.length && !f.rules.every((r) => l.rules.includes(r))) return false;
		return true;
	});
}

export function sortLobbies(list: BrowseLobby[], sortBy: SortKey): BrowseLobby[] {
	return [...list].sort((a, b) => {
		if (sortBy === "emptiest") return openSlots(b) - openSlots(a);
		return filled(b) - filled(a);
	});
}
