import { describe, it, expect, beforeEach, afterEach, vi } from "vitest";

vi.mock("$lib/stores/ws.svelte", () => ({
	ServerAction: {
		Success: "success",
		Error: "error",
		MatchStateUpdated: "match_state_updated",
		MatchOver: "match_over"
	},
	ClientAction: {
		MatchPlayCard: "match_play_card",
		MatchDrawCard: "match_draw_card",
		MatchSubmitInput: "match_submit_input"
	},
	ws: {
		on(action: string, handler: any) {
			if (!(globalThis as any).__wsTestHandlers) {
				(globalThis as any).__wsTestHandlers = {};
			}
			(globalThis as any).__wsTestHandlers[action] = handler;
			return () => {};
		},
		onOpen() {
			return () => {};
		},
		onClose() {
			return () => {};
		},
		emit() {},
		emitAndWait: async () => ({ ok: false })
	}
}));

import { storeGame } from "$lib/stores/game.svelte";

const NOW = 1_000_000;

function sendSnapshot(fields: Record<string, unknown>) {
	const handler = (globalThis as any).__wsTestHandlers?.["match_state_updated"];
	if (!handler) throw new Error("MatchStateUpdated handler was not registered");
	handler({
		match_state: {
			current_player: "alice",
			players: [],
			window: null,
			prompts: [],
			...fields
		},
		action_required: null
	});
}

describe("storeGame keeps the turn and prompt clocks separate", () => {
	beforeEach(() => {
		vi.useFakeTimers();
		vi.setSystemTime(NOW);
		storeGame.state = null;
	});

	afterEach(() => {
		vi.useRealTimers();
	});

	it("freezes the turn countdown while the turn clock is paused", () => {
		sendSnapshot({ turn_deadline_ms: NOW + 15_000 });
		vi.advanceTimersByTime(3_000);
		expect(storeGame.turnTimeRemaining).toBe(12);

		sendSnapshot({ turn_deadline_ms: 0 });
		vi.advanceTimersByTime(5_000);
		expect(storeGame.turnTimeRemaining).toBe(12);
	});

	it("counts a prompt down on its own deadline", () => {
		sendSnapshot({ turn_deadline_ms: NOW + 15_000 });
		vi.advanceTimersByTime(3_000);

		sendSnapshot({
			turn_deadline_ms: 0,
			prompts: [
				{
					prompt_id: "choose_color",
					kind: "choose_color",
					payload: {},
					response_schema: {},
					duration_ms: 15_000,
					deadline_ms: NOW + 3_000 + 15_000
				}
			]
		});
		expect(storeGame.activeTimer).toMatchObject({
			source: "prompt",
			deadlineAt: NOW + 3_000 + 15_000
		});

		vi.advanceTimersByTime(4_000);
		expect(storeGame.turnTimeRemaining).toBe(12);

		sendSnapshot({ turn_deadline_ms: 0, prompts: [] });
		expect(storeGame.activePrompt).toBeNull();
		expect(storeGame.activeTimer).toBeNull();
	});
});
