/**
 * @file logText.ts
 * @brief Resolves the text of a play log line from the texts of the `uni` mod.
 *
 * Every play log and reaction text lives in `mods/uni/texts/<locale>.json`.
 * The key of a text is `uni:log.<name>`. It replaces the interface key
 * `log_<name>` one to one: `log_reverse_back_2` became `uni:log.reverse_back_2`,
 * `log_color_red` became `uni:log.color_red`. The variants of one joke keep
 * their numbered names (`..._1`, `..._2`), and `logBuilder` lists them per rule
 * and group.
 */

import { cardInfoByKind } from "$lib/glossary/cardDescriptions";
import { storeModTexts } from "$lib/stores/modTexts.svelte";
import type { LogLine } from "./logEvent";

/** The locale the caller renders in; the mod text layer follows the active locale. */
export interface LogTextOptions {
	locale?: string;
}

const COLOR_PARAM = "color";
const COLOR_NAME_PARAM = "colorName";
const COLOR_NAME_KEY_PREFIX = "uni:log.color_";
const PREV_COLOR_PARAM = "prevColor";
const PREV_COLOR_NAME_PARAM = "prevColorName";
const KIND_PARAM = "kind";
const CARD_NAME_PARAM = "cardName";

/** The text of a key, or null while it loads or when no locale holds it (the layer then echoes the key). */
function textOf(key: string, params: LogLine["params"]): string | null {
	const text = storeModTexts.resolve(key, params);
	return text === key ? null : text;
}

/** The reader's word for a colour id; the id itself when the mod has no word. */
function colorWord(colorId: string): string {
	return textOf(`${COLOR_NAME_KEY_PREFIX}${colorId}`, {}) ?? colorId;
}

/**
 * Adds `cardName`, the reader's word for the card kind in `kind`, so copy can
 * name the played card while still tagging it with the raw kind. Unknown kinds
 * fall back to the kind itself.
 */
function withCardName(params: LogLine["params"]): LogLine["params"] {
	const kind = params[KIND_PARAM];
	if (typeof kind !== "string" || CARD_NAME_PARAM in params) return params;
	const info = cardInfoByKind(kind);
	return { [CARD_NAME_PARAM]: info?.title ?? kind, ...params };
}

/**
 * Adds `colorName`, the reader's word for a card colour id in `color`, so copy
 * can say the colour while still tagging it with the raw id. Unknown ids fall
 * back to the id itself.
 */
function withColorName(params: LogLine["params"]): LogLine["params"] {
	const colorId = params[COLOR_PARAM];
	if (typeof colorId !== "string" || COLOR_NAME_PARAM in params) return params;
	return {
		[COLOR_NAME_PARAM]: colorWord(colorId),
		...params
	};
}

/**
 * Adds `prevColorName`, the reader's word for a previously chosen wild colour
 * in `prevColor`, so copy can contrast it with the current one. Unknown ids
 * fall back to the id itself.
 */
function withPrevColorName(params: LogLine["params"]): LogLine["params"] {
	const colorId = params[PREV_COLOR_PARAM];
	if (typeof colorId !== "string" || PREV_COLOR_NAME_PARAM in params) return params;
	return {
		[PREV_COLOR_NAME_PARAM]: colorWord(colorId),
		...params
	};
}

/**
 * Localized copy for a play-log key, or null when the texts lack it or still
 * load. Pass the reactive locale in `options` so callers re-render on a
 * language switch.
 */
export function resolveLogText(
	key: string,
	params: LogLine["params"],
	_options: LogTextOptions = {}
): string | null {
	return textOf(key, withPrevColorName(withCardName(withColorName(params))));
}
