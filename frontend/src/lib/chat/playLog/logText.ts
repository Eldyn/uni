import * as messages from "$lib/paraglide/messages.js";
import type { LogLine } from "./logEvent";

/** Paraglide message options; `locale` pins the language for one call. */
export interface LogTextOptions {
	locale?: string;
}

type MessageFn = (params: LogLine["params"], options?: LogTextOptions) => string;

const catalog = messages as unknown as Record<string, MessageFn | undefined>;

const COLOR_PARAM = "color";
const COLOR_NAME_PARAM = "colorName";
const COLOR_NAME_KEY_PREFIX = "log_color_";

/**
 * Adds `colorName`, the reader's word for a card colour id in `color`, so copy
 * can say the colour while still tagging it with the raw id. Unknown ids fall
 * back to the id itself.
 */
function withColorName(params: LogLine["params"], options: LogTextOptions): LogLine["params"] {
	const colorId = params[COLOR_PARAM];
	if (typeof colorId !== "string" || COLOR_NAME_PARAM in params) return params;
	const colorName = catalog[`${COLOR_NAME_KEY_PREFIX}${colorId}`];
	return {
		[COLOR_NAME_PARAM]: typeof colorName === "function" ? colorName({}, options) : colorId,
		...params
	};
}

/**
 * Localized copy for a play-log key, or null when the locale lacks it. Pass
 * the reactive locale in `options` so callers re-render on a language switch.
 */
export function resolveLogText(
	key: string,
	params: LogLine["params"],
	options: LogTextOptions = {}
): string | null {
	const message = catalog[key];
	return typeof message === "function" ? message(withColorName(params, options), options) : null;
}
