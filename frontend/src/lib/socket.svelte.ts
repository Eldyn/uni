type ServerAction = "queried" | "sync_data" | "sync_count" | string;
type ConnectionStatus = "disconnected" | "connecting" | "connected";
type MessageHandler = (data: Record<string, unknown>) => void;

let ws = $state<WebSocket | null>(null);
let _status = $state<ConnectionStatus>("disconnected");
let _username = $state("");
let _room = $state("");

// INFO: Maps actions, to a set of unique callbacks to call when receiving said action.
const handlers = new Map<string, Set<MessageHandler>>();

function dispatch(data: Record<string, unknown>) {
    const action = data.action as string;

    // Call specific action handlers
    handlers.get(action)?.forEach((h) => h(data));
    // Call wildcard handlers
    handlers.get("*")?.forEach((h) => h(data));
}

export function on(action: ServerAction | "*", handler: MessageHandler) {
    if (!handlers.has(action)) handlers.set(action, new Set());
    handlers.get(action)!.add(handler);

    return () => handlers.get(action)?.delete(handler);
}

export async function connect(): Promise<void> {
    if (ws?.readyState === WebSocket.OPEN) return;

    const protocol = location.protocol === "https:" ? "wss:" : "ws:";
    const url = `${protocol}//${location.host}`;

    ws = new WebSocket(url);
    _status = "connecting";

    return new Promise((resolve, reject) => {
        ws!.onopen = () => {
            _status = "connected";
            resolve();
        };
        ws!.onerror = reject;
        ws!.onclose = () => {
            _status = "disconnected";
            ws = null;
        };
        ws!.onmessage = (e: MessageEvent) => {
            try {
                const data = JSON.parse(e.data);
                if (data.action === "sync_data") {
                    _username = data.username;
                    _room = data.room;
                }
                dispatch(data);
            } catch {
                /* ignore non-JSON */
            }
        };
    });
}

export function emit(action: string, payload: object = {}) {
    if (ws?.readyState === WebSocket.OPEN)
        ws.send(JSON.stringify({ action, ...payload }));
}

export async function emitAndWait(
    action: string,
    payload: object,
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

export const socketState = {
    get status() {
        return _status;
    },
    get username() {
        return _username;
    },
    get room() {
        return _room;
    },
};
