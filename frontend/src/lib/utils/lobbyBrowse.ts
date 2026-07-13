/**
 * @file lobbyBrowse.ts
 * @brief Pure view-model helpers for the lobby browse screen: mapping,
 * filtering, sorting and per-card join affordances. No Svelte state, unit
 * testable in isolation.
 */

import type { SortKey } from "$lib/data/lobbyCatalogs";
import { MAX_LOBBY_MEMBERS } from "$lib/generated/schemas";
import type { ListedLobby } from "$lib/stores/lobby.svelte";

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
		max: MAX_LOBBY_MEMBERS,
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

export function joinInfo(l: BrowseLobby): JoinInfo {
	if (l.status === "in-game") {
		if (l.allowBotTakeover && l.bots > 0)
			return {
				dot: "bg-orange-400",
				label: "Join",
				bg: "bg-orange-500",
				disabled: false,
				title: "In game, joinable by replacing a bot"
			};
		return {
			dot: "bg-red-500",
			label: null,
			bg: "",
			disabled: true,
			title: "Match in progress"
		};
	}
	if (l.status === "full")
		return {
			dot: "bg-zinc-500",
			label: "Full",
			bg: "bg-surface-2",
			disabled: true,
			title: "Lobby is full"
		};
	return {
		dot: "bg-green-500",
		label: "Play",
		bg: "bg-accent",
		disabled: false,
		title: "Open, join now"
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
