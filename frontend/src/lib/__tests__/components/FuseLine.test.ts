import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { render, fireEvent } from "@testing-library/svelte";
import { flushSync } from "svelte";

vi.mock("$lib/stores/ws.svelte", () => ({
	ServerAction: {
		Success: "success",
		Error: "error",
		LobbyList: "lobby_list",
		LobbyJoined: "lobby_joined",
		LobbyUpdated: "lobby_updated",
		LobbyLeft: "lobby_left",
		LobbyEvicted: "lobby_evicted",
		MatchStateUpdated: "match_state_updated",
		MatchEvent: "match_event",
		MatchOver: "match_over",
		ChatMessage: "chat_message",
		ChatHistory: "chat_history",
		FriendList: "friend_list",
		Metadata: "metadata"
	},
	ClientAction: {
		MatchPlayCard: "match_play_card",
		MatchDrawCard: "match_draw_card",
		MatchClientReady: "match_client_ready",
		MatchWindowResponse: "match_window_response"
	},
	ws: {
		on(action: string, handler: any) {
			if (!(globalThis as any).__wsTestHandlers) {
				(globalThis as any).__wsTestHandlers = {};
			}
			(globalThis as any).__wsTestHandlers[action] = handler;
			return () => {};
		},
		onOpen: () => () => {},
		onClose: () => () => {},
		emit() {},
		emitAndWait: async () => ({ ok: false })
	}
}));

import FuseLine from "$components/game/FuseLine.svelte";
import { storeGame } from "$stores/game.svelte";
import { storeAuth } from "$stores/auth.svelte";
import { storeLobby } from "$stores/lobby.svelte";
import { storeDebug } from "$stores/debug.svelte";
import { storeWebglCapability } from "$stores/webglCapability.svelte";

const NOW = 2_000_000;

function handler(action: string): (payload: any) => void {
	return (globalThis as any).__wsTestHandlers[action];
}

function frame(seq: number, type: string, payload: unknown = {}) {
	return { action: "match_event", seq, type, payload };
}

function snapshot(fields: Record<string, unknown> = {}, statuses: unknown[] = []) {
	handler("match_state_updated")({
		match_state: {
			current_player: "bob",
			players: [
				{ username: "alice", card_count: 5, is_bot: false, statuses },
				{ username: "bob", card_count: 5, is_bot: false }
			],
			draw_pile: { count: 40 },
			...fields
		},
		action_required: null
	});
}

const DEBT = [{ status_kind: "vanilla:draw_debt", magnitude: 2 }];

const HELD_WINDOW = {
	window_id: "7",
	kind: "jump_in",
	kinds: ["jump_in", "generic"],
	hold_ms: 800,
	duration_ms: 7000,
	deadline_ms: 7000,
	responders: ["alice", "carol"],
	eligible_filter_digest: "d"
};

function advance(ms: number) {
	vi.advanceTimersByTime(ms);
	flushSync();
}

function fillScale(container: HTMLElement): number {
	const fill = container.querySelector<HTMLElement>('[data-testid="fuse-fill"]')!;
	return parseFloat(/scaleX\(([\d.]+)\)/.exec(fill.style.transform)![1]);
}

describe("FuseLine", () => {
	let passSpy: ReturnType<typeof vi.spyOn>;

	beforeEach(() => {
		vi.useFakeTimers();
		vi.setSystemTime(NOW);
		storeAuth.username = "alice";
		storeLobby.current = null;
		storeDebug.enabled = false;
		storeWebglCapability.reducedMotion = false;
		storeGame.reset();
		passSpy = vi.spyOn(storeGame, "passWindow").mockImplementation(() => {});
	});

	afterEach(() => {
		passSpy.mockRestore();
		storeDebug.enabled = false;
		storeWebglCapability.reducedMotion = false;
		vi.useRealTimers();
		document.body.innerHTML = "";
	});

	it("renders nothing when no timer is armed", () => {
		const { container } = render(FuseLine);

		expect(container.querySelector('[data-testid="fuse-line"]')).toBeNull();
	});

	it("drains the window fill from the payload duration", () => {
		handler("match_event")(frame(1, "window_open", HELD_WINDOW));
		const { container } = render(FuseLine);

		expect(container.querySelector('[data-source="window"]')).not.toBeNull();
		expect(fillScale(container)).toBeCloseTo(1, 2);

		advance(3500);
		expect(fillScale(container)).toBeCloseTo(0.5, 1);
	});

	it("restores a mid-window snapshot to the correct remaining fill", () => {
		snapshot({
			server_now_ms: 900_000,
			window: { ...HELD_WINDOW, deadline_ms: 900_000 + 1750, duration_ms: 7000 }
		});
		const { container } = render(FuseLine);

		expect(fillScale(container)).toBeCloseTo(0.25, 2);
	});

	it("marks the hold at holdMs of the duration", () => {
		handler("match_event")(frame(1, "window_open", HELD_WINDOW));
		const { container } = render(FuseLine);

		const tick = container.querySelector<HTMLElement>('[data-testid="fuse-hold-tick"]')!;
		expect(parseFloat(tick.style.left)).toBeCloseTo((1 - 800 / 7000) * 100, 2);

		expect(container.querySelector('[data-hold="active"]')).not.toBeNull();
		advance(800);
		expect(container.querySelector('[data-hold="elapsed"]')).not.toBeNull();
	});

	it("draws no hold marker for a window without a hold", () => {
		handler("match_event")(
			frame(1, "window_open", {
				window_id: "1",
				deadline_ms: 5000,
				responders: ["bob"],
				eligible_filter_digest: "d"
			})
		);
		const { container } = render(FuseLine);

		expect(container.querySelector('[data-testid="fuse-hold-tick"]')).toBeNull();
	});

	it("draws the turn clock when no window is open", () => {
		storeLobby.current = { settings: { turn_time_limit_ms: 20_000 } } as any;
		snapshot({ turn_deadline_ms: NOW + 10_000 });
		const { container } = render(FuseLine);

		expect(container.querySelector('[data-source="turn"]')).not.toBeNull();
		expect(fillScale(container)).toBeCloseTo(0.5, 2);
	});

	it("draws the prompt clock", () => {
		handler("match_event")(
			frame(1, "prompt_open", {
				prompt_id: "p",
				kind: "choose_color",
				payload: {},
				response_schema: {},
				duration_ms: 8000,
				deadline_ms: 0
			})
		);
		const { container } = render(FuseLine);

		expect(container.querySelector('[data-source="prompt"]')).not.toBeNull();
		advance(2000);
		expect(fillScale(container)).toBeCloseTo(0.75, 1);
	});

	it("draws the ready barrier clock", () => {
		handler("match_event")(frame(1, "players_ready", { ready: 1, total: 2, timeout_ms: 15_000 }));
		const { container } = render(FuseLine);

		expect(container.querySelector('[data-source="ready"]')).not.toBeNull();
	});

	it("shows the window, not the turn, while both exist", () => {
		snapshot({ turn_deadline_ms: NOW + 10_000 });
		handler("match_event")(frame(1, "window_open", HELD_WINDOW));
		const { container } = render(FuseLine);

		expect(container.querySelector('[data-source="window"]')).not.toBeNull();
		expect(container.querySelector('[data-source="turn"]')).toBeNull();
	});

	describe("draw action", () => {
		it("is disabled before the hold and enabled after, for the debt victim", async () => {
			snapshot({}, DEBT);
			handler("match_event")(frame(1, "window_open", HELD_WINDOW));
			const { container } = render(FuseLine);

			const draw = container.querySelector<HTMLButtonElement>('[data-testid="fuse-action"]')!;
			expect(draw.textContent?.trim()).toBe("Draw");
			expect(draw.disabled).toBe(true);

			advance(799);
			expect(draw.disabled).toBe(true);
			advance(100);
			expect(draw.disabled).toBe(false);

			await fireEvent.click(draw);
			expect(passSpy).toHaveBeenCalledTimes(1);
		});

		it("is absent for a non-victim responder", () => {
			snapshot({}, []);
			handler("match_event")(frame(1, "window_open", HELD_WINDOW));
			const { container } = render(FuseLine);

			expect(container.querySelector('[data-testid="fuse-action"]')).toBeNull();
		});

		it("keeps the legacy Pass for a responder of a window without hold", async () => {
			snapshot({}, []);
			handler("match_event")(
				frame(1, "window_open", {
					window_id: "2",
					deadline_ms: 5000,
					responders: ["alice"],
					eligible_filter_digest: "d"
				})
			);
			const { container } = render(FuseLine);

			const pass = container.querySelector<HTMLButtonElement>('[data-testid="fuse-action"]')!;
			expect(pass.textContent?.trim()).toBe("Pass");
			expect(pass.disabled).toBe(false);
			await fireEvent.click(pass);
			expect(passSpy).toHaveBeenCalledTimes(1);
		});
	});

	describe("debug label", () => {
		it("shows the joined kinds only when debug is on", () => {
			handler("match_event")(frame(1, "window_open", HELD_WINDOW));
			const { container } = render(FuseLine);

			expect(container.querySelector('[data-testid="fuse-debug"]')).toBeNull();

			storeDebug.enabled = true;
			flushSync();
			expect(container.querySelector('[data-testid="fuse-debug"]')?.textContent).toBe(
				"jump_in+generic"
			);
		});

		it("shows nothing for a non-window timer even when debug is on", () => {
			storeDebug.enabled = true;
			snapshot({ turn_deadline_ms: NOW + 10_000 });
			const { container } = render(FuseLine);

			expect(container.querySelector('[data-testid="fuse-debug"]')).toBeNull();
		});
	});

	describe("reduced motion", () => {
		it("steps the fill instead of animating it", () => {
			storeWebglCapability.reducedMotion = true;
			handler("match_event")(frame(1, "window_open", HELD_WINDOW));
			const { container } = render(FuseLine);

			expect(container.querySelector('[data-motion="reduced"]')).not.toBeNull();
			advance(3000);
			const scale = fillScale(container);
			expect(scale).toBeCloseTo(0.6, 5);
			expect((scale * 10) % 1).toBeCloseTo(0, 5);
		});
	});

	describe("accessibility", () => {
		it("carries a visually hidden role=status label naming the timer", () => {
			handler("match_event")(frame(1, "window_open", HELD_WINDOW));
			const { container } = render(FuseLine);

			const status = container.querySelector('[role="status"]')!;
			expect(status.textContent?.trim()).toBe("Response window");
			expect(status.classList.contains("visually-hidden")).toBe(true);
			expect(
				container.querySelector('[data-testid="fuse-track"]')?.getAttribute("aria-hidden")
			).toBe("true");
		});

		it("names each audited timer", () => {
			snapshot({ turn_deadline_ms: NOW + 10_000 });
			const { container } = render(FuseLine);

			expect(container.querySelector('[role="status"]')?.textContent?.trim()).toBe("Turn timer");
		});
	});
});
