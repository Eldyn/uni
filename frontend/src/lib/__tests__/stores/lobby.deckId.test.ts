import { describe, it, expect, vi, afterEach } from "vitest";
import { ws, ClientAction } from "$lib/stores/ws.svelte";
import { LobbyUpdateSettingsMessageSchema } from "$lib/generated/schemas";

const wsAny = ws as unknown as { socket: WebSocket | null };

afterEach(() => {
	wsAny.socket = null;
	vi.restoreAllMocks();
});

describe("lobby_update_settings deck_id transport", () => {
	it("the generated outgoing schema preserves deck_id instead of stripping it", () => {
		const result = LobbyUpdateSettingsMessageSchema.safeParse({
			action: "lobby_update_settings",
			deck_id: "mod:deck"
		});

		expect(result.success).toBe(true);
		expect(result.data?.deck_id).toBe("mod:deck");
	});

	it("validates and sends an outgoing frame carrying deck_id", () => {
		const send = vi.fn();
		wsAny.socket = { readyState: WebSocket.OPEN, send } as unknown as WebSocket;

		ws.emit(ClientAction.LobbyUpdateSettings, { deck_id: "mod:deck" });

		expect(send).toHaveBeenCalledTimes(1);
		expect(JSON.parse(send.mock.calls[0][0])).toEqual({
			action: "lobby_update_settings",
			deck_id: "mod:deck"
		});
	});

	it("blocks an outgoing frame whose deck_id has the wrong type", () => {
		const errorSpy = vi.spyOn(console, "error").mockImplementation(() => {});
		const send = vi.fn();
		wsAny.socket = { readyState: WebSocket.OPEN, send } as unknown as WebSocket;

		ws.emit(ClientAction.LobbyUpdateSettings, { deck_id: 123 } as never);

		expect(send).not.toHaveBeenCalled();
		expect(errorSpy).toHaveBeenCalled();
	});
});
