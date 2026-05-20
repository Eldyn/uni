/**
 * WebSocket communication utilities
 * Manages connection, message handlers, and emit/wait patterns
 */

import type { ClientActionType, ServerActionType } from './protocol';

type ServerAction = ServerActionType | string;
type MessageHandler = (data: Record<string, unknown>) => void;

interface ConnectionStatus {
	status: 'disconnected' | 'connecting' | 'connected';
	username: string;
	room: string;
}

let ws = $state<WebSocket | null>(null);
let connectionStatus = $state<ConnectionStatus>({
	status: 'disconnected',
	username: '',
	room: '',
});

// Maps actions to sets of unique callbacks to call when receiving said action
const handlers = new Map<string, Set<MessageHandler>>();

function dispatch(data: Record<string, unknown>) {
	const action = data.action as string;

	// Call specific action handlers
	handlers.get(action)?.forEach((h) => h(data));
	// Call wildcard handlers
	handlers.get('*')?.forEach((h) => h(data));
}

/**
 * Register a handler for a specific action or all actions
 * Returns cleanup function
 */
export function on(action: ServerAction | '*', handler: MessageHandler) {
	if (!handlers.has(action)) handlers.set(action, new Set());
	handlers.get(action)!.add(handler);

	return () => handlers.get(action)?.delete(handler);
}

/**
 * Connect to WebSocket server
 */
export async function connect(): Promise<void> {
	if (ws?.readyState === WebSocket.OPEN) return;

	const protocol = location.protocol === 'https:' ? 'wss:' : 'ws:';
	const url = `${protocol}//${location.host}`;

	ws = new WebSocket(url);
	connectionStatus.status = 'connecting';

	return new Promise((resolve, reject) => {
		ws!.onopen = () => {
			connectionStatus.status = 'connected';
			resolve();
		};
		ws!.onerror = (event) => {
			reject(new Error('WebSocket connection failed'));
		};
		ws!.onclose = () => {
			connectionStatus.status = 'disconnected';
			ws = null;
		};
		ws!.onmessage = (e: MessageEvent) => {
			try {
				const data = JSON.parse(e.data);
				if (data.action === 'sync_data') {
					connectionStatus.username = data.username || '';
					connectionStatus.room = data.room || '';
				}
				dispatch(data);
			} catch {
				// Ignore non-JSON messages
			}
		};
	});
}

/**
 * Disconnect from WebSocket server
 */
export function disconnect(): void {
	if (ws) {
		ws.close();
		ws = null;
		connectionStatus.status = 'disconnected';
	}
}

/**
 * Emit a message to the server
 */
export function emit(action: string, payload: Record<string, unknown> = {}): void {
	if (ws?.readyState === WebSocket.OPEN) {
		ws.send(JSON.stringify({ action, ...payload }));
	}
}

/**
 * Emit a message and wait for a specific response action
 */
export async function emitAndWait(
	action: string,
	payload: Record<string, unknown> = {},
	expectedAction: string,
	timeoutMs = 5000,
): Promise<Record<string, unknown>> {
	return new Promise((resolve, reject) => {
		const timer = setTimeout(() => {
			cleanup();
			reject(new Error(`Timeout waiting for ${expectedAction}`));
		}, timeoutMs);

		const cleanup = on(expectedAction, (data) => {
			clearTimeout(timer);
			cleanup();
			resolve(data);
		});

		emit(action, payload);
	});
}

/**
 * Check if WebSocket is connected
 */
export function isConnected(): boolean {
	return ws?.readyState === WebSocket.OPEN;
}

/**
 * Get current connection status (reactive)
 */
export function getConnectionStatus(): Readonly<ConnectionStatus> {
	return connectionStatus;
}
