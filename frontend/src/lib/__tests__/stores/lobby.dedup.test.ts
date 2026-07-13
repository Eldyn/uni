import { describe, it, expect } from "vitest";
import { ws } from "$lib/stores/ws.svelte";

// Access private fields via any-cast for the dedup regression assertion.
const wsAny = ws as unknown as {
	onHandlers: Map<string, Set<(...args: unknown[]) => unknown>>;
	openHandlers: Array<() => void | Promise<void>>;
};

describe("WS subscription dedup", () => {
	it("Set deduplicates the same function reference", () => {
		const key = "__dedup_test__";
		const handler = () => {};
		const unsub1 = ws.on(key as Parameters<typeof ws.on>[0], handler);
		const unsub2 = ws.on(key as Parameters<typeof ws.on>[0], handler);

		expect(wsAny.onHandlers.get(key)?.size).toBe(1);

		unsub1();
		unsub2();
		expect(wsAny.onHandlers.get(key)?.size ?? 0).toBe(0);
	});

	it("lobby store registers handlers once regardless of reconnect count", async () => {
		// Importing the lobby store triggers its constructor, which calls
		// #registerListeners() exactly once (latch pattern).
		await import("$lib/stores/lobby.svelte");

		const before = wsAny.onHandlers.get("lobby_updated")?.size ?? 0;
		expect(before).toBeGreaterThan(0);

		// Simulate 3 reconnect cycles by firing all onOpen handlers.
		for (let i = 0; i < 3; i++) {
			for (const h of [...wsAny.openHandlers]) {
				try {
					await h();
				} catch {
					// tryRejoin will fail without a real socket, ignore
				}
			}
		}

		const after = wsAny.onHandlers.get("lobby_updated")?.size ?? 0;
		expect(after).toBe(before);
	});
});
