import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { createMatchEventBeatController } from "$components/game/animation/matchEventController.svelte";
import type { CardBus } from "$components/game/card-bus.svelte";
import type { CardRegistry } from "$components/game/animation/cardRegistry.svelte";
import type { BoardPlacement } from "$components/game/layout/boardPlacement";
import { mapMatchEventPacket, type MatchEventBeat } from "$stores/matchEventMap";
import { storeGame } from "$stores/game.svelte";
import { storeAuth } from "$stores/auth.svelte";
import { storeLobby } from "$stores/lobby.svelte";
import { chatStore } from "$stores/chat.svelte";
import { storeAnimation } from "$stores/animation.svelte";
import { storeSpectator } from "$stores/spectator.svelte";
import { storeAudio } from "$stores/audio.svelte";
import { bindingRegistry } from "$lib/presentation/eventBindings";
import { stableIndex } from "$lib/chat/playLog/stableIndex";

type WirePackets = Record<"actor" | "other_seat" | "public_spectator", Record<string, unknown>>;
type DefsGolden = { presentations: Record<string, unknown>[] };

const wireFiles = import.meta.glob<WirePackets>(
	"../../../../../tests/fixtures/golden_mod_event/wire_packets.json",
	{ eager: true, import: "default" }
);
const defsFiles = import.meta.glob<DefsGolden>(
	"../../../../../tests/fixtures/golden_presentation_binding/defs_test_mod_bindings.json",
	{ eager: true, import: "default" }
);

const wire = Object.values(wireFiles)[0]!;
const defsGolden = Object.values(defsFiles)[0]!;
const BINDING_ID = "test:apple_eaten_shown";
const YOU_KEYS = ["test_apple_eaten_you_1", "test_apple_eaten_you_2"];

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
		pendingLocalDrawIds: new Set<number>(),
		pendingLocalPlayDrawnId: null,
		pendingLocalDragPlay: null,
		reshuffleDrawPileSize: null,
		onReshuffleCardLanding: null,
		getDrawPileHoverDipZ: () => 0
	} as unknown as CardBus;
}

function harness() {
	const enqueue = vi.fn().mockReturnValue(Promise.resolve());
	const registry = { enqueue, isFastForwarding: false } as unknown as CardRegistry;
	let beatHandler: ((beat: MatchEventBeat) => void) | null = null;
	const controller = createMatchEventBeatController({
		bus: fakeBus(),
		cardRegistry: registry,
		getPlacement: () => placement,
		getOpponentSeatAnchor: () => [0, 0, 0],
		subscribeBeats: (cb) => {
			beatHandler = cb;
			return () => {};
		}
	});
	return {
		controller,
		enqueue,
		deliver: (packet: Record<string, unknown>) => {
			const beat = mapMatchEventPacket(packet);
			expect(beat).not.toBeNull();
			beatHandler!(beat!);
		}
	};
}

const flush = () => new Promise((resolve) => setTimeout(resolve, 0));
const partyLines = () => chatStore.linesFor("party");

function seatAs(username: string | null) {
	storeAuth.username = username ?? "watcher";
	storeGame.state = {
		active_type: "red",
		current_turn: "player0",
		play_direction: 1,
		top_card: { id: 1, type: "red", value: "7" },
		players: ["player0", "player1"].map((name) => ({
			username: name,
			card_count: 4,
			is_bot: false,
			hand: []
		})),
		pending_draws: 0,
		draw_pile_size: 10
	} as never;
	storeLobby.current = {
		invite_code: "ABC123",
		members: username === null ? [{ username: "watcher", is_spectator: true }] : []
	} as unknown as typeof storeLobby.current;
}

describe("a mod event through the presentation binding", () => {
	let playSfx: ReturnType<typeof vi.spyOn>;

	beforeEach(() => {
		chatStore.reset();
		storeAnimation.enabled = false;
		bindingRegistry.ingestDefs({ presentations: defsGolden.presentations });
		playSfx = vi.spyOn(storeAudio, "playSfx").mockImplementation(() => undefined);
	});

	afterEach(() => {
		bindingRegistry.clear();
		storeGame.state = null;
		storeLobby.current = null;
		storeSpectator.reset();
		chatStore.reset();
		playSfx.mockRestore();
	});

	it("loads the binding of the test mod from the stored defs manifest", () => {
		expect(bindingRegistry.forEvent("test:apple_eaten").map((b) => b.id)).toEqual([BINDING_ID]);
	});

	it("shows the actor an own beat, an own sound, and an own log line", async () => {
		seatAs("player0");
		const h = harness();

		h.deliver(wire.actor);
		await flush();

		expect(h.enqueue).toHaveBeenCalledTimes(1);
		expect(h.enqueue.mock.calls[0]![0]).toEqual([
			[{ op: "shake", target: "player0", payload: { intensity: 0.5 } }]
		]);
		expect(playSfx).toHaveBeenCalledWith("sfx.ui.tick", { pitch: undefined, volume: 0.8 });
		const [line] = partyLines();
		expect(partyLines()).toHaveLength(1);
		expect(line!.logKey).toBe(YOU_KEYS[stableIndex(0, BINDING_ID, YOU_KEYS.length)]);
		expect(line!.logParams).toEqual({ count: 3, glossary: "test:apple" });
	});

	it("shows another seat a different line and no beat", async () => {
		seatAs("player1");
		const h = harness();

		h.deliver(wire.other_seat);
		await flush();

		expect(h.enqueue).not.toHaveBeenCalled();
		expect(playSfx).toHaveBeenCalledWith("roll", { pitch: undefined, volume: 0.4 });
		expect(partyLines().map((line) => line.logKey)).toEqual(["test_apple_eaten_other"]);
		expect(partyLines()[0]!.logParams).toEqual({ name: "player0", count: 3 });
	});

	it("shows a spectator the line for other seats", async () => {
		seatAs(null);
		const h = harness();

		h.deliver(wire.public_spectator);
		await flush();

		expect(partyLines().map((line) => line.logKey)).toEqual(["test_apple_eaten_other"]);
		expect(h.enqueue).not.toHaveBeenCalled();
	});

	it("gives the same event a different result for each viewer", async () => {
		const outcomes: string[] = [];
		for (const [seat, packet] of [
			["player0", wire.actor],
			["player1", wire.other_seat]
		] as const) {
			chatStore.reset();
			seatAs(seat);
			harness().deliver(packet);
			await flush();
			outcomes.push(partyLines()[0]!.logKey ?? "");
		}
		expect(outcomes[0]).not.toBe(outcomes[1]);
	});

	it("presents nothing for an event of the replayed tail", async () => {
		seatAs("player0");
		const h = harness();

		h.deliver({ ...wire.actor, replayed: true });
		await flush();

		expect(h.enqueue).not.toHaveBeenCalled();
		expect(playSfx).not.toHaveBeenCalled();
		expect(partyLines()).toEqual([]);
	});

	it("presents nothing when the match has no bindings", async () => {
		bindingRegistry.clear();
		seatAs("player0");
		const h = harness();

		h.deliver(wire.actor);
		await flush();

		expect(h.enqueue).not.toHaveBeenCalled();
		expect(partyLines()).toEqual([]);
	});

	it("posts the lines in event order", async () => {
		seatAs("player0");
		const h = harness();

		h.deliver({ ...wire.actor, sequence: 4 });
		h.deliver({ ...wire.actor, sequence: 5 });
		await flush();

		expect(partyLines()).toHaveLength(2);
		expect(partyLines()[0]!.logKey).toBe(YOU_KEYS[stableIndex(4, BINDING_ID, 2)]);
		expect(partyLines()[1]!.logKey).toBe(YOU_KEYS[stableIndex(5, BINDING_ID, 2)]);
	});
});
