import * as messages from "$lib/paraglide/messages.js";
import type { LogLine } from "./logEvent";

type MessageFn = (params: LogLine["params"]) => string;

const catalog = messages as unknown as Record<string, MessageFn | undefined>;

/** Localized copy for a play-log key, or null when the locale lacks it. */
export function resolveLogText(key: string, params: LogLine["params"]): string | null {
	const message = catalog[key];
	return typeof message === "function" ? message(params) : null;
}
