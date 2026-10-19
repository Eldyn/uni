/**
 * @file profileModel.ts
 * @brief Pure helpers that compute the profile screen's derived values (win rate,
 * rank label, cards played groups, podium layout) from a PlayerStats snapshot.
 */

import type { PlayerStats } from "$stores/stats.svelte";
import type { CardValue } from "$stores/game.svelte";

/** One bar or card in a cards played chart: its key, raw count and relative size. */
export type CardsPlayedRow = {
	key: string;
	count: number;
	ratio: number;
};

/** Identifier of a cards played group. */
export type CardsPlayedGroupId = "colours" | "values" | "actions";

/** A group of rows rendered together in the cards played chart. */
export type CardsPlayedGroup = {
	id: CardsPlayedGroupId;
	rows: CardsPlayedRow[];
};

/** Colour card keys, in their natural display order. */
const COLOUR_KEYS = ["red", "blue", "green", "yellow"] as const;

/** Total matches played: wins plus losses. */
export function gamesPlayed(stats: PlayerStats): number {
	return stats.total_wins + stats.total_losses;
}

/** Rounded win rate in percent, or null when no match has been played. */
export function winRatePercent(stats: PlayerStats): number | null {
	const games = gamesPlayed(stats);
	if (games === 0) return null;
	return Math.round((stats.total_wins / games) * 100);
}

/** A usable leaderboard position, or null for unranked (0, null, undefined). */
export function rankDisplay(rank: number | null | undefined): number | null {
	if (rank === 0 || rank === null || rank === undefined) return null;
	return rank;
}

/** Splits the `cards_played_` counters into colours, values and actions groups. */
export function cardsPlayedGroups(stats: PlayerStats): CardsPlayedGroup[] {
	const counters = new Map<string, number>();
	for (const key of Object.keys(stats)) {
		if (!key.startsWith("cards_played_")) continue;
		const value = stats[key as keyof PlayerStats];
		if (typeof value === "number") counters.set(key.slice("cards_played_".length), value);
	}
	if (counters.size === 0 || counters.values().every((count) => count === 0)) return [];

	const colourRows: CardsPlayedRow[] = [];
	const valueRows: CardsPlayedRow[] = [];
	const actionRows: CardsPlayedRow[] = [];

	for (const colour of COLOUR_KEYS) {
		addRow(colourRows, colour, counters.get(colour));
	}
	for (const [key, count] of counters) {
		if (isDigitKey(key)) addRow(valueRows, key, count);
	}
	valueRows.sort((a, b) => Number(a.key) - Number(b.key));
	const digitKeys = new Set(valueRows.map((row) => row.key));
	const actionEntries = [...counters].filter(([key]) => !isColourKey(key) && !digitKeys.has(key));
	for (const [key, count] of actionEntries) {
		addRow(actionRows, key, count);
	}
	const sortedActions = sortByCountDesc(actionRows);

	const groups: CardsPlayedGroup[] = [];
	for (const [id, rows] of [
		["colours", colourRows],
		["values", valueRows],
		["actions", sortedActions]
	] as const) {
		if (rows.some((row) => row.count > 0)) {
			groups.push({ id, rows: scaleRatios(rows) });
		}
	}
	return groups;
}

/** Stable descending sort by count: ties keep their input order. */
function sortByCountDesc(rows: CardsPlayedRow[]): CardsPlayedRow[] {
	return rows
		.map((row, index) => ({ row, index }))
		.sort((a, b) => b.row.count - a.row.count || a.index - b.index)
		.map(({ row }) => row);
}

function isColourKey(key: string): boolean {
	return (COLOUR_KEYS as readonly string[]).includes(key);
}

function isDigitKey(key: string): boolean {
	return /^[0-9]+$/.test(key);
}

function addRow(rows: CardsPlayedRow[], key: string, count: number | undefined): void {
	if (count !== undefined) rows.push({ key, count, ratio: 0 });
}

/** Normalises each group's ratios against its own biggest counter. */
function scaleRatios(rows: CardsPlayedRow[]): CardsPlayedRow[] {
	const max = Math.max(...rows.map((row) => row.count), 0);
	if (max === 0) return rows;
	return rows.map((row) => ({ ...row, ratio: row.count / max }));
}

/**
 * Arranges rows for the podium display: the centre row (or the biggest one)
 * in the middle, the others alternating left then right so the counts fall
 * off with distance from the centre.
 */
export function podiumOrder(rows: CardsPlayedRow[], centerKey?: string): CardsPlayedRow[] {
	if (rows.length === 0) return [];
	const centre =
		rows.find((row) => row.key === centerKey) ??
		rows.reduce((best, row) => (row.count > best.count ? row : best), rows[0]);
	const others = sortByCountDesc(rows.filter((row) => row !== centre));

	const left: CardsPlayedRow[] = [];
	const right: CardsPlayedRow[] = [];
	for (const [index, row] of others.entries()) {
		if (index % 2 === 0) left.push(row);
		else right.push(row);
	}
	return [...left.reverse(), centre, ...right];
}

/**
 * How much more (or less) the row `key` was played than the average of the
 * other rows, in percent; null without a usable baseline.
 */
export function percentVersusOthers(rows: CardsPlayedRow[], key: string): number | null {
	const target = rows.find((row) => row.key === key);
	const others = rows.filter((row) => row.key !== key);
	if (!target || others.length === 0) return null;
	const average = others.reduce((sum, row) => sum + row.count, 0) / others.length;
	if (average === 0) return null;
	return Math.round((target.count / average - 1) * 100);
}

/** Card face value drawn for an action row key, or null when no face exists. */
export function actionCardValue(key: string): CardValue | null {
	switch (key) {
		case "skip":
			return "skip";
		case "reverse":
			return "reverse";
		case "draw2":
			return "+2";
		case "draw4":
			return "jolly_draw4";
		case "jolly":
			return "jolly";
		default:
			return null;
	}
}
