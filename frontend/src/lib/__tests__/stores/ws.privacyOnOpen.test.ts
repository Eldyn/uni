import { describe, it, expect, vi, afterEach } from "vitest";
import { ClientAction, ws } from "$lib/stores/ws.svelte";

const wsAny = ws as unknown as {
	openHandlers: Array<() => void | Promise<void>>;
};

describe("ws store: privacy mode is re-sent on open", () => {
	afterEach(() => {
		localStorage.removeItem("uni_privacy_mode");
		vi.restoreAllMocks();
	});

	it("the module-level onOpen handler emits the stored privacy mode", async () => {
		localStorage.setItem("uni_privacy_mode", "true");
		const emit = vi.spyOn(ws, "emit").mockImplementation(() => {});

		for (const handler of wsAny.openHandlers) await handler();

		expect(emit).toHaveBeenCalledWith(ClientAction.UserUpdatePrivacy, { privacy_mode: true });
	});
});
