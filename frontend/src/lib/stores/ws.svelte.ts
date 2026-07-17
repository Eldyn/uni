/**
 * @file ws.svelte.ts
 * @brief Communication utility and wrapper for the WebSocket protocol.
 * Handles the connection, message routing (handlers) and implements a
 * synchronous/asynchronous pattern (emit/wait) using Svelte 5 reactive state.
 *
 * Action constants and payload types are generated from contract/asyncapi.yaml.
 * Run `npm run generate:contract` to regenerate after schema changes.
 */

export {
	ClientAction,
	ServerAction,
	type ClientActionType,
	type ServerActionType,
	type ClientPayloads
} from "$lib/generated/schemas";

import type { ClientPayloads, ServerActionType } from "$lib/generated/schemas";
import { outgoingSchemas } from "$lib/generated/schemas";
import { storeAnalytics } from "./analytics.svelte";
import { errorText } from "./errors";
import { z } from "zod";

/** Base schema for parsing incoming server frames. */
const IncomingMessageSchema = z.looseObject({
	action: z.string(),
	request_id: z.string().optional()
});

export type ServerActionDef = ServerActionType | string;

export type MessageHandler = (data: Record<string, unknown>) => void;

/**
 * @interface ConnectionStatus
 * @brief Represents the reactive state of the WebSocket connection.
 */
export interface ConnectionStatus {
	status: "disconnected" | "connecting" | "connected";
	username: string;
	room: string;
	lobby_code: string;
}

/**
 * @class WsResponse
 * @brief Unified wrapper for WebSocket message responses.
 * @tag FRONT-WS-001
 */
export class WsResponse<T extends Record<string, unknown> = Record<string, unknown>> {
	public readonly ok: boolean;
	public readonly action: string;
	public readonly request_id?: string;
	public readonly data: T;

	constructor(rawData: T) {
		this.data = rawData;
		this.action = (rawData.action as string) || "";
		this.request_id = rawData.request_id as string;
		this.ok = this.action !== "error";
	}

	/** The raw ErrorCode wire string, when this is an error frame. */
	get code(): string | undefined {
		return this.data.code as string | undefined;
	}

	/** Human-readable text for this response, resolved from its error code. */
	get message(): string {
		return errorText(this.code, this.data.detail as string | undefined);
	}

	get<V>(key: string): V | null {
		return key in this.data ? (this.data[key] as V) : null;
	}

	getOr<V>(key: string, fallback: V): V {
		return key in this.data ? (this.data[key] as V) : fallback;
	}
}

interface PendingRequest {
	resolve: (res: WsResponse) => void;
	reject: (err: Error) => void;
	timer: ReturnType<typeof setTimeout>;
}

/**
 * @class WebSocketClient
 * @brief Stateful and reactive WebSocket client with Zod-validated boundaries.
 * @tag FRONT-WS-002
 */
export class WebSocketClient {
	socket = $state<WebSocket | null>(null);

	connectionStatus = $state<ConnectionStatus>({
		status: "disconnected",
		username: "",
		room: "",
		lobby_code: ""
	});

	private _nextRequestId = 1;
	private pendingRequests = new Map<string, PendingRequest>();
	private onHandlers = new Map<string, Set<MessageHandler>>();
	private openHandlers: Array<() => void | Promise<void>> = [];
	private closeHandlers: Array<() => void | Promise<void>> = [];
	private visibilityListenerAttached = false;
	private reconnectTimer: ReturnType<typeof setTimeout> | null = null;
	private reconnectDelayMs = 1000;
	private intentionalClose = false;
	/**
	 * Sockets closed via disconnect(), checked (and cleared) by that specific
	 * socket's own onclose so a disconnect()-then-connect() pair (e.g.
	 * auth.svelte.ts re-upgrading the identity) can't race: without this, the
	 * old socket's onclose firing after the new connect() already reset the
	 * shared `intentionalClose` flag back to false would read as an organic
	 * drop and schedule a spurious extra reconnect, permanently orphaning a
	 * socket that stays subscribed to broadcast topics (duplicate chat
	 * messages, one per login/logout cycle).
	 */
	private intentionallyClosedSockets = new WeakSet<WebSocket>();
	/** In-flight connection attempt, shared so concurrent connect() calls
	 *  never open a second socket while the first is still handshaking. */
	private connectPromise: Promise<void> | null = null;

	get isConnected(): boolean {
		return this.socket?.readyState === WebSocket.OPEN;
	}

	async connect(): Promise<void> {
		if (this.isConnected) return;
		if (this.connectPromise) return this.connectPromise;
		// INFO: A pending auto-retry (scheduled by a previous drop/rejection)
		//       must not survive an explicit connect(): left alone, it can fire
		//       independently later and open a second, uncoordinated socket
		//       alongside this one, both live and subscribed under the same
		//       identity (duplicate chat delivery).
		if (this.reconnectTimer) {
			clearTimeout(this.reconnectTimer);
			this.reconnectTimer = null;
		}
		this.intentionalClose = false;
		this.#attachVisibilityListener();
		this.connectPromise = this._connectOnce().finally(() => {
			this.connectPromise = null;
		});
		await this.connectPromise;
	}

	disconnect(code: number, reason: string): void {
		this.intentionalClose = true;

		if (this.reconnectTimer) {
			clearTimeout(this.reconnectTimer);
			this.reconnectTimer = null;
		}

		if (this.socket) {
			this.intentionallyClosedSockets.add(this.socket);
			this.socket.close(code, reason);
		}
		this.socket = null;
		this.connectionStatus.status = "disconnected";

		for (const handler of this.closeHandlers) {
			try {
				handler();
			} catch (e) {
				console.error("onClose handler failed:", e);
			}
		}
	}

	/**
	 * @brief Fire-and-forget send. Validates the outgoing payload with Zod in dev mode.
	 */
	emit<K extends keyof ClientPayloads>(
		action: K,
		...args: ClientPayloads[K] extends Record<string, never>
			? [payload?: Record<string, never>]
			: [payload: ClientPayloads[K]]
	): void {
		const payload = args[0] ?? {};
		const frame = { action, ...payload };
		this.#validateAndSend(String(action), frame);
	}

	/**
	 * @brief Sends a message and awaits the correlated server response.
	 */
	emitAndWait<K extends keyof ClientPayloads>(
		action: K,
		payload?: ClientPayloads[K],
		timeoutMs: number = 5000
	): Promise<WsResponse> {
		const actualPayload = payload ?? {};
		return new Promise((resolve, reject) => {
			const requestId = String(this._nextRequestId++);

			const timer = setTimeout(() => {
				this.pendingRequests.delete(requestId);
				reject(new Error(`Timeout: no response for "${String(action)}" (request_id=${requestId})`));
			}, timeoutMs);

			this.pendingRequests.set(requestId, { resolve, reject, timer });

			if (this.isConnected) {
				const frame = { action, request_id: requestId, ...actualPayload };
				this.#validateAndSend(String(action), frame);
			} else {
				this.pendingRequests.delete(requestId);
				clearTimeout(timer);
				reject(new Error("WebSocket not connected"));
			}
		});
	}

	on(action: ServerActionDef | "*", handler: MessageHandler): () => void {
		if (!this.onHandlers.has(action)) this.onHandlers.set(action, new Set());
		this.onHandlers.get(action)!.add(handler);
		return () => this.onHandlers.get(action)?.delete(handler);
	}

	onOpen(handler: () => void | Promise<void>): () => void {
		this.openHandlers.push(handler);
		return () => {
			this.openHandlers = this.openHandlers.filter((h) => h !== handler);
		};
	}

	onClose(handler: () => void | Promise<void>): () => void {
		this.closeHandlers.push(handler);
		return () => {
			this.closeHandlers = this.closeHandlers.filter((h) => h !== handler);
		};
	}

	// --- Private ---

	/** Validates an outgoing frame with its Zod message schema (if one exists), then sends. */
	#validateAndSend(action: string, frame: Record<string, unknown>): void {
		if (!this.isConnected) return;

		const schema = outgoingSchemas[action];
		if (schema) {
			const result = schema.safeParse(frame);
			if (!result.success) {
				console.error(
					`[ws] Outgoing validation failed for action "${action}":`,
					result.error.issues
				);
				return; // Block malformed frames from reaching the server
			}
		}

		this.socket!.send(JSON.stringify(frame));
	}

	private async _connectOnce(): Promise<void> {
		return new Promise(async (resolve, reject) => {
			const protocol = location.protocol === "https:" ? "wss:" : "ws:";
			const wsInstance = new WebSocket(`${protocol}//${location.host}`);
			this.connectionStatus.status = "connecting";

			wsInstance.onopen = async () => {
				this.socket = wsInstance;
				this.connectionStatus.status = "connected";
				this.reconnectDelayMs = 1000;

				for (const handler of this.openHandlers) {
					try {
						await handler();
					} catch (e) {
						console.error("onOpen handler failed:", e);
					}
				}

				resolve();
			};

			wsInstance.onerror = () => reject(new Error("Could not connect to the server."));

			wsInstance.onclose = () => {
				const wasIntentional = this.intentionallyClosedSockets.delete(wsInstance);

				if (this.socket === wsInstance) {
					this.socket = null;
					this.connectionStatus.status = "disconnected";
				}

				for (const [, pending] of this.pendingRequests) {
					clearTimeout(pending.timer);
					pending.reject(new Error("disconnected"));
				}
				this.pendingRequests.clear();

				if (!wasIntentional) this._scheduleReconnect();
			};

			wsInstance.onmessage = (e: MessageEvent) => {
				let raw: unknown;
				try {
					raw = JSON.parse(e.data as string);
				} catch {
					console.error("[ws] Incoming frame is not valid JSON");
					return;
				}
				const parsed = IncomingMessageSchema.safeParse(raw);
				if (!parsed.success) {
					console.error("[ws] Incoming frame failed schema check:", parsed.error.issues);
					return;
				}
				const data = parsed.data as Record<string, unknown>;

				if (data.action === "sync_data") {
					this.connectionStatus.username = data.username as string;
				}
				this._dispatch(data);
			};
		});
	}

	private _dispatch(data: Record<string, unknown>): void {
		const action = data.action as string;
		const requestId = data.request_id as string;

		let consumedByRequest = false;
		if (requestId && this.pendingRequests.has(requestId)) {
			const pending = this.pendingRequests.get(requestId)!;
			this.pendingRequests.delete(requestId);
			clearTimeout(pending.timer);
			pending.resolve(new WsResponse(data));
			consumedByRequest = true;
		}

		// An error already delivered to its awaiter (emitAndWait) is handled by
		// the caller; don't also fan it out to the global "error" middleware, or
		// the toast would fire twice. The middleware is for unsolicited errors.
		if (action === "error" && consumedByRequest) return;

		if (action) {
			// Isolate handlers: one throwing listener must not starve the others.
			const safeCall = (h: MessageHandler) => {
				try {
					h(data);
				} catch (e) {
					console.error(`[ws] Handler for "${action}" threw:`, e);
				}
			};
			this.onHandlers.get(action)?.forEach(safeCall);
			this.onHandlers.get("*")?.forEach(safeCall);
		}
	}

	private _scheduleReconnect(): void {
		if (this.intentionalClose) return;
		if (this.reconnectTimer) clearTimeout(this.reconnectTimer);

		this.reconnectTimer = setTimeout(() => {
			this.reconnectTimer = null;
			if (document.visibilityState === "hidden") return;
			const elapsedDelayMs = this.reconnectDelayMs;
			this.reconnectDelayMs = Math.min(this.reconnectDelayMs * 2, 16_000);
			storeAnalytics.track("ws_reconnect", { delay_ms: elapsedDelayMs });
			this.connect().catch(() => this._scheduleReconnect());
		}, this.reconnectDelayMs);
	}

	#attachVisibilityListener(): void {
		if (this.visibilityListenerAttached) return;
		this.visibilityListenerAttached = true;

		document.addEventListener("visibilitychange", () => {
			if (document.visibilityState === "visible" && !this.isConnected && !this.intentionalClose) {
				this.reconnectDelayMs = 1000;
				this.connect().catch(() => this._scheduleReconnect());
			}
		});
	}
}

export const ws = new WebSocketClient();
