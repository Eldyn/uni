import { describe, it, expect } from "vitest";
import type { GamePlayer } from "$stores/game.svelte";
import {
	resolveViewedPlayer,
	resolvePovPlayer,
	hiddenBackCountFor,
	hiddenBackCard,
	HIDDEN_BACK_ID_BASE
} from "$components/game/layout/spectatorPov";

function player(username: string, overrides: Partial<GamePlayer> = {}): GamePlayer {
	return { username, card_count: 0, is_bot: false, ...overrides };
}

describe("resolveViewedPlayer", () => {
	const alice = player("alice", { card_count: 3, hand: [] });
	const bob = player("bob", { card_count: 5 });
	const carol = player("carol", { card_count: 2 });

	it("prefers the explicitly viewed username", () => {
		expect(resolveViewedPlayer([alice, bob], "bob", "alice")?.username).toBe("bob");
	});

	it("falls back to the current turn when no explicit choice exists", () => {
		expect(resolveViewedPlayer([alice, bob, carol], null, "carol")?.username).toBe("carol");
	});

	it("falls back to the first player when neither resolves", () => {
		expect(resolveViewedPlayer([alice, bob], null, null)?.username).toBe("alice");
	});

	it("falls back to the first player when the viewed username is unknown", () => {
		expect(resolveViewedPlayer([alice, bob], "ghost", "bob")?.username).toBe("alice");
	});

	it("returns null with no players", () => {
		expect(resolveViewedPlayer([], "bob", "bob")).toBeNull();
	});
});

describe("resolvePovPlayer", () => {
	const alice = player("alice", { card_count: 3, hand: [] });
	const bob = player("bob", { card_count: 5 });
	const carol = player("carol", { card_count: 2 });
	const roster = [alice, bob, carol];

	it("returns the local player for a non-spectator", () => {
		expect(resolvePovPlayer(alice, false, roster, "bob", "carol")?.username).toBe("alice");
	});

	it("returns null for a non-spectator with no known local player", () => {
		expect(resolvePovPlayer(null, false, roster, null, "carol")).toBeNull();
	});

	it("returns the explicitly viewed player for a spectator", () => {
		expect(resolvePovPlayer(null, true, roster, "bob", "carol")?.username).toBe("bob");
	});

	it("falls back to the current turn for a spectator with no explicit view", () => {
		expect(resolvePovPlayer(null, true, roster, null, "carol")?.username).toBe("carol");
	});

	it("returns null for a spectator with an empty roster", () => {
		expect(resolvePovPlayer(null, true, [], "bob", "carol")).toBeNull();
	});
});

describe("hiddenBackCountFor", () => {
	const visibleHand = [{ id: 1, type: "red", value: "3" }];

	it("is 0 for a present, non-empty hand (faces are known)", () => {
		expect(hiddenBackCountFor(player("alice", { card_count: 1, hand: visibleHand }))).toBe(0);
	});

	it("uses card_count when the hand is absent", () => {
		expect(hiddenBackCountFor(player("alice", { card_count: 4 }))).toBe(4);
	});

	it("uses card_count when the hand is sent empty", () => {
		expect(hiddenBackCountFor(player("alice", { card_count: 4, hand: [] }))).toBe(4);
	});

	it("is 0 when the hidden player holds no cards", () => {
		expect(hiddenBackCountFor(player("alice", { card_count: 0 }))).toBe(0);
	});

	it("is 0 for a missing player", () => {
		expect(hiddenBackCountFor(null)).toBe(0);
		expect(hiddenBackCountFor(undefined)).toBe(0);
	});
});

describe("hiddenBackCard", () => {
	it("produces unique, stable, facedown ids in their own namespace", () => {
		const first = hiddenBackCard(0);
		const second = hiddenBackCard(1);
		expect(first.id).toBe(HIDDEN_BACK_ID_BASE);
		expect(second.id).toBe(HIDDEN_BACK_ID_BASE - 1);
		expect(first.id).not.toBe(second.id);
		// Never collides with baseBeats.ts's reshuffle synthetics (-1000..).
		expect(first.id).toBeLessThan(-1000);
		expect(first.type).toBe("wild");
		expect(first.value).toBe("0");
	});
});
