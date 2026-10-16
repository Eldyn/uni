/**
 * @file pseudoLocale.ts
 * @brief The run-time `pseudo` locale for mod texts.
 *
 * The interface texts of the pseudo locale are a file made by hand. The mod
 * texts have no such file: the layer takes the `en` pattern, accents the
 * literal text, and pads the result, so a developer sees layout overflow and
 * untranslated strings.
 */

const ACCENTED_LETTERS: Readonly<Record<string, string>> = {
	a: "å",
	e: "é",
	i: "î",
	o: "ö",
	u: "ü",
	c: "ç",
	n: "ñ",
	s: "š",
	y: "ý",
	A: "Å",
	E: "É",
	I: "Î",
	O: "Ö",
	U: "Ü",
	C: "Ç",
	N: "Ñ",
	S: "Š",
	Y: "Ý"
};

const RICH_TEXT_TAG = /^\[\/?(?:c|fx|k)(?:=[^\]]*)?\]/;
const ESCAPE_CHAR = "\\";
const OPEN_MARK = "«";
const CLOSE_MARK = "»";
const PADDING_CHAR = "~";
const PADDING_RATIO = 0.3;

/** Accents the letters of a literal text piece. Rich text tags and escapes stay. */
export function pseudoLocalizeText(text: string): string {
	let output = "";
	let index = 0;
	while (index < text.length) {
		const char = text[index]!;
		if (char === ESCAPE_CHAR && index + 1 < text.length) {
			output += char + text[index + 1]!;
			index += 2;
			continue;
		}
		if (char === "[") {
			const tag = RICH_TEXT_TAG.exec(text.slice(index));
			if (tag) {
				output += tag[0];
				index += tag[0].length;
				continue;
			}
		}
		output += ACCENTED_LETTERS[char] ?? char;
		index += 1;
	}
	return output;
}

/** Marks the ends of a formatted string and pads it to show overflow. */
export function wrapPseudo(formatted: string): string {
	const padding = PADDING_CHAR.repeat(Math.ceil(formatted.length * PADDING_RATIO));
	return `${OPEN_MARK}${formatted}${padding}${CLOSE_MARK}`;
}
