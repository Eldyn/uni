import * as messages from "$lib/paraglide/messages.js";
import { cardInfoByKind } from "$lib/glossary/cardDescriptions";
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
const PREV_COLOR_PARAM = "prevColor";
const PREV_COLOR_NAME_PARAM = "prevColorName";
const KIND_PARAM = "kind";
const CARD_NAME_PARAM = "cardName";

/**
 * Adds `cardName`, the reader's word for the card kind in `kind`, so copy can
 * name the played card while still tagging it with the raw kind. Unknown kinds
 * fall back to the kind itself.
 */
function withCardName(params: LogLine["params"], options: LogTextOptions): LogLine["params"] {
	const kind = params[KIND_PARAM];
	if (typeof kind !== "string" || CARD_NAME_PARAM in params) return params;
	const info = cardInfoByKind(kind, options.locale);
	return { [CARD_NAME_PARAM]: info?.title ?? kind, ...params };
}

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
 * Adds `prevColorName`, the reader's word for a previously chosen wild colour
 * in `prevColor`, so copy can contrast it with the current one. Unknown ids
 * fall back to the id itself.
 */
function withPrevColorName(params: LogLine["params"], options: LogTextOptions): LogLine["params"] {
	const colorId = params[PREV_COLOR_PARAM];
	if (typeof colorId !== "string" || PREV_COLOR_NAME_PARAM in params) return params;
	const colorName = catalog[`${COLOR_NAME_KEY_PREFIX}${colorId}`];
	return {
		[PREV_COLOR_NAME_PARAM]: typeof colorName === "function" ? colorName({}, options) : colorId,
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
	return typeof message === "function"
		? message(
				withPrevColorName(withCardName(withColorName(params, options), options), options),
				options
			)
		: null;
}
