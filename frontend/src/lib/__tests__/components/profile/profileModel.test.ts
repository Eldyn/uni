import { describe, it, expect } from "vitest";
import type { PlayerStats } from "$stores/stats.svelte";
import {
	actionCardValue,
	cardsPlayedGroups,
	gamesPlayed,
	percentVersusOthers,
	podiumOrder,
	rankDisplay,
	winRatePercent
} from "$components/profile/profileModel";

const stats = (partial: Partial<PlayerStats>): PlayerStats => ({
	username: "eldyn",
	total_wins: 0,
	total_losses: 0,
	rank: null,
	...partial
});

describe("profileModel", () => {
	it("gamesPlayed sums wins and losses", () => {
		expect(gamesPlayed(stats({ total_wins: 12, total_losses: 8 }))).toBe(20);
		expect(gamesPlayed(stats({ total_wins: 0, total_losses: 0 }))).toBe(0);
	});

	it("winRatePercent is null with zero games", () => {
		expect(winRatePercent(stats({ total_wins: 0, total_losses: 0 }))).toBeNull();
	});

	it("winRatePercent rounds 12 wins 8 losses to 60", () => {
		expect(winRatePercent(stats({ total_wins: 12, total_losses: 8 }))).toBe(60);
	});

	it("rankDisplay maps 0, null and undefined to null and keeps 3", () => {
		expect(rankDisplay(0)).toBeNull();
		expect(rankDisplay(null)).toBeNull();
		expect(rankDisplay(undefined)).toBeNull();
		expect(rankDisplay(3)).toBe(3);
	});

	it("cardsPlayedGroups returns [] when all counters are zero", () => {
		expect(
			cardsPlayedGroups(
				stats({
					cards_played_red: 0,
					cards_played_blue: 0,
					cards_played_0: 0,
					cards_played_skip: 0
				})
			)
		).toEqual([]);
	});

	it("cardsPlayedGroups returns [] when no counters exist", () => {
		expect(cardsPlayedGroups(stats({}))).toEqual([]);
	});

	it("cardsPlayedGroups groups colours values and actions in that order", () => {
		const groups = cardsPlayedGroups(
			stats({
				cards_played_red: 41,
				cards_played_blue: 33,
				cards_played_yellow: 0,
				cards_played_0: 2,
				cards_played_1: 9,
				cards_played_5: 4,
				cards_played_skip: 18,
				cards_played_draw2: 9,
				cards_played_draw4: 31,
				cards_played_jolly: 7
			})
		);

		expect(groups.map((group) => group.id)).toEqual(["colours", "values", "actions"]);
		const [colours, values, actions] = groups;
		expect(colours.rows.map((row) => row.key)).toEqual(["red", "blue", "yellow"]);
		expect(values.rows.map((row) => row.key)).toEqual(["0", "1", "5"]);
		expect(actions.rows.map((row) => row.key)).toEqual(["draw4", "skip", "draw2", "jolly"]);
	});

	it("cardsPlayedGroups ratio is relative to the biggest counter of its own group", () => {
		const groups = cardsPlayedGroups(
			stats({
				cards_played_red: 40,
				cards_played_blue: 20,
				cards_played_skip: 30,
				cards_played_draw4: 10
			})
		);

		const colours = groups.find((group) => group.id === "colours");
		const actions = groups.find((group) => group.id === "actions");
		expect(colours?.rows.find((row) => row.key === "red")?.ratio).toBe(1);
		expect(colours?.rows.find((row) => row.key === "blue")?.ratio).toBeCloseTo(0.5);
		expect(actions?.rows.find((row) => row.key === "skip")?.ratio).toBe(1);
		expect(actions?.rows.find((row) => row.key === "draw4")?.ratio).toBeCloseTo(1 / 3);
	});

	it("podiumOrder puts the biggest in the middle and alternates the rest", () => {
		const rows = [
			{ key: "a", count: 50, ratio: 1 },
			{ key: "b", count: 30, ratio: 0.6 },
			{ key: "c", count: 20, ratio: 0.4 },
			{ key: "d", count: 10, ratio: 0.2 },
			{ key: "e", count: 5, ratio: 0.1 }
		];

		expect(podiumOrder(rows).map((row) => row.key)).toEqual(["d", "b", "a", "c", "e"]);
	});

	it("podiumOrder centres a chosen key", () => {
		const rows = [
			{ key: "a", count: 50, ratio: 1 },
			{ key: "b", count: 30, ratio: 0.6 },
			{ key: "c", count: 20, ratio: 0.4 },
			{ key: "d", count: 10, ratio: 0.2 },
			{ key: "e", count: 5, ratio: 0.1 }
		];

		expect(podiumOrder(rows, "c").map((row) => row.key)).toEqual(["d", "a", "c", "b", "e"]);
	});

	it("percentVersusOthers returns 40 for 14 against others averaging 10", () => {
		const rows = [
			{ key: "a", count: 14, ratio: 1 },
			{ key: "b", count: 10, ratio: 0.7 },
			{ key: "c", count: 10, ratio: 0.7 }
		];

		expect(percentVersusOthers(rows, "a")).toBe(40);
	});

	it("percentVersusOthers returns a negative number below the average", () => {
		const rows = [
			{ key: "a", count: 10, ratio: 0.5 },
			{ key: "b", count: 20, ratio: 1 },
			{ key: "c", count: 20, ratio: 1 }
		];

		expect(percentVersusOthers(rows, "a")).toBe(-50);
	});

	it("percentVersusOthers returns null with no other rows", () => {
		expect(percentVersusOthers([{ key: "a", count: 5, ratio: 1 }], "a")).toBeNull();
	});

	it("percentVersusOthers returns null when the others average 0", () => {
		const rows = [
			{ key: "a", count: 5, ratio: 1 },
			{ key: "b", count: 0, ratio: 0 }
		];

		expect(percentVersusOthers(rows, "a")).toBeNull();
	});
});

describe("actionCardValue", () => {
	it("maps each action key to its card face value", () => {
		expect(actionCardValue("skip")).toBe("skip");
		expect(actionCardValue("reverse")).toBe("reverse");
		expect(actionCardValue("draw2")).toBe("+2");
		expect(actionCardValue("draw4")).toBe("jolly_draw4");
		expect(actionCardValue("jolly")).toBe("jolly");
	});

	it("returns null for a key without a card face", () => {
		expect(actionCardValue("mystery")).toBeNull();
	});
});
