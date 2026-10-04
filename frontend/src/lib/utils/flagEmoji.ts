/**
 * @file flagEmoji.ts
 * @brief Detects whether the platform draws flag emoji as flags.
 *
 * Without a colour emoji font (e.g. Windows), a flag sequence falls back to two
 * boxed letters. A real flag is a single glyph, so it measures narrower than its
 * two regional-indicator letters drawn separately.
 */

const PROBE_FONT = "32px sans-serif";
const FLAG_GLYPH_WIDTH_RATIO = 0.9;

let cachedSupport: boolean | undefined;

function detectFlagEmojiSupport(): boolean {
	const context = document.createElement("canvas").getContext("2d");
	if (!context) return true;

	context.font = PROBE_FONT;
	const flagWidth = context.measureText("🇬🇧").width;
	const separateLettersWidth = context.measureText("🇬").width + context.measureText("🇧").width;
	if (separateLettersWidth === 0) return true;

	return flagWidth < separateLettersWidth * FLAG_GLYPH_WIDTH_RATIO;
}

export function supportsFlagEmoji(): boolean {
	cachedSupport ??= detectFlagEmojiSupport();
	return cachedSupport;
}
