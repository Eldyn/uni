import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { createMatchEventBeatController } from "$components/game/animation/matchEventController.svelte";
import type { MatchEventBeat } from "$components/game/animation/baseBeats.svelte";
import type { CardBus } from "$components/game/card-bus.svelte";
import { storeGame } from "$stores/game.svelte";
import { storeAuth } from "$stores/auth.svelte";
import { storeLobby } from "$stores/lobby.svelte";
import { chatStore } from "$stores/chat.svelte";
import { storeAnimation } from "$stores/animation.svelte";
import { storeSpectator } from "$stores/spectator.svelte";
import type { BoardPlacement } from "$components/game/layout/boardPlacement";

const placement: BoardPlacement = {
	mat: {
		size: [10, 6],
		offsetX: 0,
		offsetZ: 0,
		quarterTurn: false,
		bounds: { left: -5, right: 5, far: -3, near: 3 }
	},
	handScale: 1,
	centerScale: 1,
	discardX: 0,
	discardZ: 0,
	localSeatZ: 5,
	localAvatarZ: 6,
	drawPileX: -3,
	drawPileZ: 5,
	drawPileScale: 1
};

function fakeBus() {
	return {
		discardHistory: [] as never[],
		localHandSnapshot: { orderIds: [] as number[], scrollEm: 0, maxHalfSpanEm: 10 },
		previousLocalHandSnapshot: null,
		setActiveType: vi.fn(),
		setDiscardTop: vi.fn(),
		addInFlightPlay: vi.fn(),
		removeInFlightPlay: vi.fn(),
		addInFlightDraw: vi.fn(),
		removeInFlightDraw: vi.fn(),
		addPendingLocalDraw: vi.fn(),
		removePendingLocalDraw: vi.fn(),
		pendingLocalDrawIds: new Set<number>(),
		pendingLocalPlayDrawnId: null,
		pendingLocalDragPlay: null,
		setPendingLocalDragPlay: vi.fn(),
		retainTopDiscard: vi.fn(),
		reshuffleDrawPileSize: null,
		onReshuffleCardLanding: null,
		getDrawPileHoverDipZ: () => 0
	} as unknown as CardBus;
}

function fakeRegistry(landing: Promise<void> = Promise.resolve()) {
	return {
		clearDecoration: vi.fn(),
		setDecoration: vi.fn(),
		seedPose: vi.fn(),
		registerCardMeta: vi.fn(),
		removeEntry: vi.fn(),
		enqueue: vi.fn().mockReturnValue(landing)
	} as unknown as import("$components/game/animation/cardRegistry.svelte").CardRegistry;
}

function stateWithTop(
	top: { id: number; type: string; value: string },
	active = "red",
	overrides: Record<string, unknown> = {}
) {
	return {
		active_type: active,
		current_turn: "bob",
		play_direction: 1,
		top_card: top,
		players: [
			{ username: "me", card_count: 4, is_bot: false, hand: [] },
			{ username: "bob", card_count: 3, is_bot: false, hand: [] }
		],
		pending_draws: 0,
		draw_pile_size: 10,
		...overrides
	} as never;
}

function harness(landing?: Promise<void>) {
	let beatHandler: ((beat: MatchEventBeat) => void) | null = null;
	let desyncHandler: (() => void) | null = null;
	const controller = createMatchEventBeatController({
		bus: fakeBus(),
		cardRegistry: fakeRegistry(landing),
		getPlacement: () => placement,
		getOpponentSeatAnchor: () => [0, 0, 0],
		subscribeBeats: (cb) => {
			beatHandler = cb;
			return () => {};
		},
		subscribeDesync: (cb) => {
			desyncHandler = cb;
			return () => {};
		}
	});
	return {
		controller,
		fire: (beat: MatchEventBeat) => beatHandler!(beat),
		desync: () => desyncHandler!()
	};
}

const skipTurn = (skipped: string[], direction = 1): MatchEventBeat => ({
	seq: 1,
	kind: "turn",
	from: "me",
	to: "carol",
	direction,
	deadlineMs: 1000,
	skipped
});

const partyKeys = () => chatStore.linesFor("party").map((line) => line.logKey);
const flush = () => new Promise((resolve) => setTimeout(resolve, 0));

describe("matchEventController play log", () => {
	beforeEach(() => {
		chatStore.reset();
		storeLobby.current = {
			invite_code: "ABC123",
			members: []
		} as unknown as typeof storeLobby.current;
		storeAuth.username = "me";
		storeAnimation.enabled = false;
	});

	afterEach(() => {
		storeGame.state = null;
		storeLobby.current = null;
		storeSpectator.reset();
		chatStore.reset();
	});

	it("posts a skip line to the party channel only", async () => {
		storeGame.state = stateWithTop({ id: 2, type: "red", value: "7" });
		const h = harness();

		h.fire(skipTurn(["bob"]));
		await flush();

		expect(partyKeys()).toEqual(["log_skip"]);
		expect(chatStore.linesFor("global")).toEqual([]);
	});

	it("posts nothing when the player is not in a lobby", async () => {
		storeLobby.current = null;
		storeGame.state = stateWithTop({ id: 2, type: "red", value: "7" });
		const h = harness();

		h.fire(skipTurn(["bob"]));
		await flush();

		expect(partyKeys()).toEqual([]);
	});

	it("holds the line back until the played card has landed", async () => {
		let land!: () => void;
		const landing = new Promise<void>((resolve) => (land = resolve));
		storeGame.state = stateWithTop({ id: 2, type: "red", value: "skip" });
		const h = harness(landing);

		h.fire({ seq: 1, kind: "play", player: "me", cardId: 2, auto: false });
		h.fire(skipTurn(["bob"]));
		await flush();
		expect(partyKeys()).toEqual([]);

		land();
		await flush();
		expect(partyKeys()).toEqual(["log_play", "log_skip"]);
	});

	it("escalates a repeated skip, and a desync resets the streak (Review Focus 5)", async () => {
		storeGame.state = stateWithTop({ id: 2, type: "red", value: "7" });
		const h = harness();

		h.fire(skipTurn(["bob"]));
		h.fire(skipTurn(["bob"]));
		await flush();
		h.desync();
		h.fire(skipTurn(["bob"]));
		await flush();

		expect(partyKeys()).toEqual(["log_skip", "log_skip_again_2", "log_skip"]);
	});

	it("resets the streak when the match is cleared", async () => {
		storeGame.state = stateWithTop({ id: 2, type: "red", value: "7" });
		const h = harness();

		h.fire(skipTurn(["bob"]));
		await flush();
		storeGame.state = null;
		h.controller.syncState();
		storeGame.state = stateWithTop({ id: 2, type: "red", value: "7" });
		h.fire(skipTurn(["bob"]));
		await flush();

		expect(partyKeys()).toEqual(["log_skip", "log_skip"]);
	});

	it("escapes a hostile name before it reaches the chat line (Review Focus 1)", async () => {
		storeGame.state = stateWithTop({ id: 2, type: "red", value: "7" });
		const h = harness();

		h.fire(skipTurn(["[fx=shake]boo[/fx]"]));
		await flush();

		const [line] = chatStore.linesFor("party");
		expect(line.logParams?.name).not.toBe("[fx=shake]boo[/fx]");
		expect(String(line.logParams?.name)).toContain("\\[");
	});

	it("logs the played card's kind from the snapshot", async () => {
		storeGame.state = stateWithTop({ id: 2, type: "red", value: "skip" });
		storeGame.state.top_card.kind = "vanilla:red_skip";
		const h = harness();

		h.fire({ seq: 7, kind: "play", player: "me", cardId: 2, auto: false });
		await flush();

		const [line] = chatStore.linesFor("party");
		expect(line.logKey).toBe("log_play");
		expect(line.logParams).toMatchObject({ kind: "vanilla:red_skip", color: "red" });
	});

	it("logs a draw stack from a +2 play and its debt toast", async () => {
		storeGame.state = stateWithTop({ id: 2, type: "red", value: "+2" });
		const h = harness();

		h.fire({ seq: 1, kind: "play", player: "me", cardId: 2, auto: false });
		h.fire({
			seq: 2,
			kind: "toast",
			target: "bob",
			statusKind: "vanilla:draw_debt",
			magnitude: 4,
			durationUnit: "none",
			instanceId: 1
		});
		await flush();

		expect(partyKeys()).toEqual(["log_play", "log_draw_stack"]);
		const line = chatStore.linesFor("party")[1];
		expect(line.logKey).toBe("log_draw_stack");
		expect(line.logParams).toMatchObject({ amount: 2, total: 4 });
	});

	it("posts a draw reaction for a local draw", async () => {
		storeGame.state = stateWithTop({ id: 2, type: "red", value: "7" });
		const h = harness();

		h.fire({ seq: 1, kind: "draw", player: "me", count: 1, sourcePile: "draw", cardIds: [99] });
		await flush();

		expect(partyKeys()).toEqual([expect.stringMatching(/^log_draw_first/)]);
	});

	it("posts a draw reaction for an opponent draw", async () => {
		storeGame.state = stateWithTop({ id: 2, type: "red", value: "7" });
		const h = harness();

		h.fire({ seq: 2, kind: "draw", player: "bob", count: 1, sourcePile: "draw", cardIds: [] });
		await flush();

		expect(partyKeys()).toEqual([expect.stringMatching(/^log_draw_first/)]);
	});

	it("posts a draw reaction for a spectator viewing the drawing player", async () => {
		storeAuth.username = "spec";
		storeSpectator.viewedUsername = "bob";
		storeGame.state = stateWithTop({ id: 2, type: "red", value: "7" }, "red", {
			current_turn: "bob",
			players: [
				{ username: "bob", card_count: 3, is_bot: false, hand: [] },
				{ username: "carol", card_count: 4, is_bot: false, hand: [] }
			]
		});
		const h = harness();

		// INFO: the server withholds card identities from a spectator, so the
		//       beat carries no ids even though bob is the viewed/"local" player.
		h.fire({ seq: 1, kind: "draw", player: "bob", count: 1, sourcePile: "draw", cardIds: [] });
		await flush();

		expect(partyKeys()).toEqual([expect.stringMatching(/^log_draw_first/)]);
	});

	it("logs a forced play as an auto play", async () => {
		storeGame.state = stateWithTop({ id: 2, type: "red", value: "7" });
		const h = harness();

		h.fire({ seq: 20, kind: "play", player: "me", cardId: 2, auto: true });
		await flush();

		expect(partyKeys()).toEqual([expect.stringMatching(/^log_auto_play/)]);
	});

	it("logs a wild colour reaction once the snapshot resolves it", async () => {
		storeGame.state = stateWithTop({ id: 2, type: "white", value: "jolly" }, "white");
		const h = harness();

		h.fire({ seq: 21, kind: "play", player: "me", cardId: 2, auto: false });
		await flush();
		expect(partyKeys()).toEqual([]);

		storeGame.state = stateWithTop({ id: 2, type: "white", value: "jolly" }, "red");
		h.controller.syncState();
		await flush();
		expect(partyKeys()).toEqual(["log_play", "log_wild_reaction_1"]);
	});

	it("logs a reshuffle reaction after the pile is reshuffled", async () => {
		storeGame.state = stateWithTop({ id: 2, type: "red", value: "7" });
		const h = harness();
		h.controller.syncState();

		h.fire({ seq: 22, kind: "reshuffle", drawSize: 13, discardSize: 3 });
		await flush();

		expect(partyKeys()).toEqual([expect.stringMatching(/^log_reshuffle/)]);
	});

	it("logs a near-win when a non-draw play leaves the actor at one card", async () => {
		storeGame.state = stateWithTop({ id: 2, type: "red", value: "7" }, "red", {
			players: [
				{ username: "me", card_count: 1, is_bot: false, hand: [] },
				{ username: "bob", card_count: 3, is_bot: false, hand: [] }
			]
		});
		const h = harness();

		h.fire({ seq: 23, kind: "play", player: "me", cardId: 2, auto: false });
		await flush();

		expect(partyKeys()).toEqual(["log_play", expect.stringMatching(/^log_near_win/)]);
	});

	it("does not log a near-win when the played card is a draw card", async () => {
		storeGame.state = stateWithTop({ id: 2, type: "red", value: "+2" }, "red", {
			players: [
				{ username: "me", card_count: 1, is_bot: false, hand: [] },
				{ username: "bob", card_count: 3, is_bot: false, hand: [] }
			]
		});
		const h = harness();

		h.fire({ seq: 24, kind: "play", player: "me", cardId: 2, auto: false });
		await flush();

		expect(partyKeys()).toEqual(["log_play"]);
	});

	it("logs a win and each later placement once from snapshot changes", async () => {
		storeGame.state = stateWithTop({ id: 2, type: "red", value: "7" }, "red", {
			winner: "bob",
			placements: ["bob", "me"]
		});
		const h = harness();

		h.controller.syncState();
		await flush();
		expect(partyKeys()).toEqual([
			expect.stringMatching(/^log_win/),
			expect.stringMatching(/^log_left_behind/)
		]);

		h.controller.syncState();
		await flush();
		expect(partyKeys()).toHaveLength(2);
	});

	it("does not log a win for an aborted match's empty winner", async () => {
		storeGame.state = stateWithTop({ id: 2, type: "red", value: "7" }, "red", {
			winner: "",
			placements: []
		});
		const h = harness();

		h.controller.syncState();
		await flush();

		expect(partyKeys()).toEqual([]);
	});

	it("logs a blocked retort when the near-win target is skipped by the last player", async () => {
		storeGame.state = stateWithTop({ id: 2, type: "red", value: "7" }, "red", {
			players: [
				{ username: "me", card_count: 4, is_bot: false, hand: [] },
				{ username: "bob", card_count: 1, is_bot: false, hand: [] },
				{ username: "ann", card_count: 3, is_bot: false, hand: [] }
			]
		});
		const h = harness();

		// bob's near-win play opens the window
		h.fire({ seq: 30, kind: "play", player: "bob", cardId: 2, auto: false });
		await flush();

		// ann plays next, becoming the last player before the skip turn
		storeGame.state = stateWithTop({ id: 3, type: "blue", value: "7" }, "blue", {
			players: [
				{ username: "me", card_count: 4, is_bot: false, hand: [] },
				{ username: "bob", card_count: 1, is_bot: false, hand: [] },
				{ username: "ann", card_count: 2, is_bot: false, hand: [] }
			]
		});
		h.fire({ seq: 31, kind: "play", player: "ann", cardId: 3, auto: false });
		await flush();

		h.fire(skipTurn(["bob"]));
		await flush();

		const keys = partyKeys();
		const blockedIndex = keys.findIndex((key) => key?.startsWith("log_blocked_"));
		expect(keys.indexOf("log_skip")).toBeGreaterThanOrEqual(0);
		expect(blockedIndex).toBeGreaterThan(keys.indexOf("log_skip"));
		const blockedLine = chatStore.linesFor("party")[blockedIndex];
		expect(blockedLine.logParams).toEqual({ blocker: "ann", target: "bob" });
	});
});
