import { describe, it, expect } from "vitest";

import {
	toBrowseLobby,
	filterLobbies,
	sortLobbies,
	joinInfo,
	openSlots,
	filled,
	category,
	type BrowseLobby,
	type BrowseFilters
} from "$lib/utils/lobbyBrowse";
import type { ListedLobby } from "$lib/stores/lobby.svelte";

function makeListed(overrides: Partial<ListedLobby> = {}): ListedLobby {
	return {
		name: "Test Lobby",
		member_count: 2,
		bot_count: 1,
		invite_code: "AAAAAA",
		status: "open",
		active_mods: [],
		allow_bot_takeover: false,
		...overrides
	};
}

function makeBrowse(overrides: Partial<BrowseLobby> = {}): BrowseLobby {
	return { ...toBrowseLobby(makeListed()), ...overrides };
}

function makeFilters(overrides: Partial<BrowseFilters> = {}): BrowseFilters {
	return {
		nameQuery: "",
		quickOpenOnly: false,
		quickHideInGame: false,
		status: { open: true, inGame: true, full: true },
		minOpenSlots: 0,
		takeoverOnly: false,
		decks: [],
		rules: [],
		...overrides
	};
}

describe("toBrowseLobby", () => {
	it("maps counts and rules through", () => {
		const l = toBrowseLobby(makeListed({ active_mods: ["seven_zero"] }));
		expect(l.humans).toBe(2);
		expect(l.bots).toBe(1);
		expect(l.rules).toEqual(["seven_zero"]);
	});

	it("guards missing counts so slot math never yields NaN (the blank-screen crash class)", () => {
		const raw = makeListed();
		// Simulate the broken payload that used to crash the browse screen.
		// @ts-expect-error deliberately undefined
		raw.member_count = undefined;
		// @ts-expect-error deliberately undefined
		raw.bot_count = undefined;

		const l = toBrowseLobby(raw);
		expect(l.humans).toBe(0);
		expect(l.bots).toBe(0);
		expect(Number.isNaN(openSlots(l))).toBe(false);
		expect(openSlots(l)).toBe(4);
	});

	it("guards missing active_mods", () => {
		const raw = makeListed();
		// @ts-expect-error deliberately undefined
		raw.active_mods = undefined;
		expect(toBrowseLobby(raw).rules).toEqual([]);
	});
});

describe("slot math", () => {
	it("filled sums humans and bots", () => {
		expect(filled(makeBrowse({ humans: 2, bots: 1 }))).toBe(3);
	});

	it("openSlots clamps at zero even when overfull", () => {
		expect(openSlots(makeBrowse({ humans: 4, bots: 2 }))).toBe(0);
	});
});

describe("category", () => {
	it.each([
		["open", "open"],
		["in-game", "inGame"],
		["full", "full"]
	] as const)("maps %s to %s", (status, expected) => {
		expect(category(makeBrowse({ status }))).toBe(expected);
	});
});

describe("joinInfo", () => {
	it("open lobby is joinable as Play", () => {
		const info = joinInfo(makeBrowse({ status: "open" }));
		expect(info.label).toBe("Play");
		expect(info.disabled).toBe(false);
	});

	it("full lobby shows disabled Full", () => {
		const info = joinInfo(makeBrowse({ status: "full" }));
		expect(info.label).toBe("Full");
		expect(info.disabled).toBe(true);
	});

	it("in-game with takeover and bots offers Join", () => {
		const info = joinInfo(makeBrowse({ status: "in-game", allowBotTakeover: true, bots: 1 }));
		expect(info.label).toBe("Join");
		expect(info.disabled).toBe(false);
	});

	it("in-game with takeover but no bots renders no button", () => {
		const info = joinInfo(makeBrowse({ status: "in-game", allowBotTakeover: true, bots: 0 }));
		expect(info.label).toBeNull();
	});

	it("in-game without takeover renders no button", () => {
		const info = joinInfo(makeBrowse({ status: "in-game", allowBotTakeover: false, bots: 2 }));
		expect(info.label).toBeNull();
		expect(info.disabled).toBe(true);
	});
});

describe("filterLobbies", () => {
	const lobbies = [
		makeBrowse({ invite_code: "OPEN01", name: "Casual Fun", status: "open", humans: 1, bots: 0 }),
		makeBrowse({ invite_code: "FULL01", name: "Packed House", status: "full", humans: 4, bots: 0 }),
		makeBrowse({
			invite_code: "GAME01",
			name: "Mid Match",
			status: "in-game",
			humans: 2,
			bots: 2,
			allowBotTakeover: true,
			rules: ["seven_zero", "stacking"]
		})
	];

	it("passes everything with default filters", () => {
		expect(filterLobbies(lobbies, makeFilters())).toHaveLength(3);
	});

	it("matches name case-insensitively", () => {
		const out = filterLobbies(lobbies, makeFilters({ nameQuery: "  cAsUaL " }));
		expect(out.map((l) => l.invite_code)).toEqual(["OPEN01"]);
	});

	it("quickOpenOnly keeps only open lobbies with free slots", () => {
		const out = filterLobbies(lobbies, makeFilters({ quickOpenOnly: true }));
		expect(out.map((l) => l.invite_code)).toEqual(["OPEN01"]);
	});

	it("quickHideInGame drops in-game lobbies", () => {
		const out = filterLobbies(lobbies, makeFilters({ quickHideInGame: true }));
		expect(out.map((l) => l.invite_code)).toEqual(["OPEN01", "FULL01"]);
	});

	it("status filter excludes deselected categories", () => {
		const out = filterLobbies(
			lobbies,
			makeFilters({ status: { open: false, inGame: true, full: false } })
		);
		expect(out.map((l) => l.invite_code)).toEqual(["GAME01"]);
	});

	it("minOpenSlots filters by free seats", () => {
		const out = filterLobbies(lobbies, makeFilters({ minOpenSlots: 3 }));
		expect(out.map((l) => l.invite_code)).toEqual(["OPEN01"]);
	});

	it("takeoverOnly keeps only takeover-enabled lobbies", () => {
		const out = filterLobbies(lobbies, makeFilters({ takeoverOnly: true }));
		expect(out.map((l) => l.invite_code)).toEqual(["GAME01"]);
	});

	it("rules filter requires every selected rule", () => {
		expect(filterLobbies(lobbies, makeFilters({ rules: ["seven_zero", "stacking"] }))).toHaveLength(
			1
		);
		expect(filterLobbies(lobbies, makeFilters({ rules: ["seven_zero", "jump_in"] }))).toHaveLength(
			0
		);
	});
});

describe("sortLobbies", () => {
	const emptyish = makeBrowse({ invite_code: "E", humans: 1, bots: 0 });
	const fullish = makeBrowse({ invite_code: "F", humans: 3, bots: 1 });

	it("fullest puts the most occupied lobby first", () => {
		expect(sortLobbies([emptyish, fullish], "fullest")[0].invite_code).toBe("F");
	});

	it("emptiest puts the most open lobby first", () => {
		expect(sortLobbies([fullish, emptyish], "emptiest")[0].invite_code).toBe("E");
	});

	it("does not mutate the input array", () => {
		const input = [emptyish, fullish];
		sortLobbies(input, "fullest");
		expect(input[0].invite_code).toBe("E");
	});
});
