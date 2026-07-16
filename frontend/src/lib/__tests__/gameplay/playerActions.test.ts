import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";

const { mockEmit } = vi.hoisted(() => ({
	mockEmit: vi.fn()
}));

vi.mock("$lib/stores/ws.svelte", () => ({
	ws: {
		emitAndWait: vi.fn(),
		emit: mockEmit,
		on: vi.fn(() => vi.fn()),
		onOpen: vi.fn(() => vi.fn()),
		connect: vi.fn()
	},
	ServerAction: {
		MatchOver: "match_over",
		MatchStateUpdated: "match_state_updated"
	},
	ClientAction: {
		MatchPlayCard: "match_play_card",
		MatchDrawCard: "match_draw_card",
		MatchSubmitInput: "match_submit_input",
		MatchExit: "match_exit"
	}
}));

import { storeGame } from "$lib/stores/game.svelte";

describe("game store: player action dispatching", () => {
	beforeEach(() => {
		vi.useFakeTimers();
		vi.clearAllMocks();
		storeGame.isActionPending = false;
	});

	afterEach(() => {
		storeGame.isActionPending = false;
		vi.useRealTimers();
	});

	it("playCard emits MatchPlayCard with card_id payload", () => {
		storeGame.playCard(42);
		expect(mockEmit).toHaveBeenCalledWith("match_play_card", { card_id: 42 });
	});

	it("playCard sets isActionPending true", () => {
		storeGame.playCard(1);
		expect(storeGame.isActionPending).toBe(true);
	});

	it("second playCard while pending is ignored", () => {
		storeGame.playCard(1);
		storeGame.playCard(2);
		expect(mockEmit).toHaveBeenCalledTimes(1);
		expect(mockEmit).toHaveBeenCalledWith("match_play_card", { card_id: 1 });
	});

	it("drawCard emits MatchDrawCard", () => {
		storeGame.drawCard();
		expect(mockEmit).toHaveBeenCalledWith("match_draw_card");
	});

	it("drawCard sets isActionPending true", () => {
		storeGame.drawCard();
		expect(storeGame.isActionPending).toBe(true);
	});

	it("second drawCard while pending is ignored", () => {
		storeGame.drawCard();
		storeGame.drawCard();
		expect(mockEmit).toHaveBeenCalledTimes(1);
	});

	it("safety timer releases isActionPending after 3001ms", () => {
		storeGame.playCard(1);
		expect(storeGame.isActionPending).toBe(true);
		vi.advanceTimersByTime(3001);
		expect(storeGame.isActionPending).toBe(false);
	});
});
