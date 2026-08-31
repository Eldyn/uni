import { describe, it, expect, vi, afterEach } from "vitest";
import { ws } from "$lib/stores/ws.svelte";

const wsAny = ws as unknown as {
	onHandlers: Map<string, Set<(data: Record<string, unknown>) => void>>;
};

afterEach(() => {
	wsAny.onHandlers.clear();
	vi.restoreAllMocks();
});

describe("ws duplicate handler detector", () => {
	it("warns when a second handler is added for the same action", () => {
		const warn = vi.spyOn(console, "warn").mockImplementation(() => {});

		ws.on("chat_message" as never, vi.fn());
		expect(warn).not.toHaveBeenCalled();

		ws.on("chat_message" as never, vi.fn());

		expect(warn).toHaveBeenCalledTimes(1);
		expect(warn.mock.calls[0][0]).toContain("chat_message");
	});

	it("does not warn for the wildcard action, which legitimately has many", () => {
		const warn = vi.spyOn(console, "warn").mockImplementation(() => {});

		ws.on("*", vi.fn());
		ws.on("*", vi.fn());

		expect(warn).not.toHaveBeenCalled();
	});
});
