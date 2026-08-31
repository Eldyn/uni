import { describe, it, expect, vi, afterEach } from "vitest";
import { ws } from "$lib/stores/ws.svelte";

// The store's private fields are reached deliberately: these tests assert
// internal invariants that have no public surface.
const wsAny = ws as unknown as {
	socket: WebSocket | null;
	connectPromise: Promise<void> | null;
	onHandlers: Map<string, Set<(data: Record<string, unknown>) => void>>;
	_connectOnce: () => Promise<void>;
};

afterEach(() => {
	wsAny.socket = null;
	wsAny.connectPromise = null;
	wsAny.onHandlers.clear();
	vi.restoreAllMocks();
});

describe("ws single-socket invariant", () => {
	it("connect() does not open a socket when one is already open", async () => {
		const connectOnce = vi.spyOn(wsAny, "_connectOnce");
		wsAny.socket = { readyState: WebSocket.OPEN } as WebSocket;

		await ws.connect();

		expect(connectOnce).not.toHaveBeenCalled();
	});

	it("concurrent connect() calls share one in-flight attempt", async () => {
		let resolveHandshake: () => void = () => {};
		const connectOnce = vi
			.spyOn(wsAny, "_connectOnce")
			.mockImplementation(
				() =>
					new Promise<void>((resolve) => {
						resolveHandshake = resolve;
					})
			);

		const first = ws.connect();
		const second = ws.connect();
		resolveHandshake();
		await Promise.all([first, second]);

		expect(connectOnce).toHaveBeenCalledTimes(1);
	});
});

describe("ws handler registration", () => {
	it("on() returns an unsubscribe that removes exactly that handler", () => {
		const first = vi.fn();
		const second = vi.fn();

		const off = ws.on("chat_message" as never, first);
		ws.on("chat_message" as never, second);
		expect(wsAny.onHandlers.get("chat_message")?.size).toBe(2);

		off();

		const remaining = wsAny.onHandlers.get("chat_message")!;
		expect(remaining.size).toBe(1);
		expect(remaining.has(second)).toBe(true);
	});
});
