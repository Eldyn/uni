import { LOG_ESCAPE_CHAR, LOG_ESCAPABLE_CHARS } from "$utils/richText";

const ESCAPABLE_PATTERN = new RegExp(
	`[${LOG_ESCAPABLE_CHARS.map((char) => `\\${char}`).join("")}]`,
	"g"
);

/** Neutralises rich-text markup in player-controlled text (names) so it renders literally in log copy. */
export function escapeLogText(input: string): string {
	return input.replace(ESCAPABLE_PATTERN, (char) => `${LOG_ESCAPE_CHAR}${char}`);
}
