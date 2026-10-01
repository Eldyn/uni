/**
 * @file richText.ts
 * @brief Parses a small inline markup grammar into flat, renderable segments.
 *
 * Grammar: `**bold**`, `*italic*`, `[c=value]...[/c]` (color), `[fx=value]...[/fx]`
 * (effect, one of TextEffects' effect names). Tags nest. No underline or
 * strikethrough tokens exist by design.
 *
 * Matching is done in two passes so malformed markup degrades to literal text
 * instead of throwing or silently swallowing the rest of the message: bold/
 * italic markers are paired sequentially (an odd one out is literal), and
 * color/effect tags are paired via a stack per tag type (an unmatched open or
 * a stray close is literal).
 */

export type RichEffect = "shake" | "undulate" | "shine";

const RICH_EFFECTS: readonly RichEffect[] = ["shake", "undulate", "shine"];
const HEX_COLOR_RE = /^#[0-9a-fA-F]{3,8}$/;
const KEYWORD_ID_RE = /^[a-zA-Z0-9_-]+$/;

export const LOG_ESCAPE_CHAR = "\\";
export const LOG_ESCAPABLE_CHARS: readonly string[] = [LOG_ESCAPE_CHAR, "[", "*"];

/** Card palette CSS variables (app.css) addressable by name in log markup. */
const LOG_COLOR_VARS: ReadonlyMap<string, string> = new Map([
	["red", "var(--redCard)"],
	["yellow", "var(--yellowCard)"],
	["green", "var(--greenCard)"],
	["blue", "var(--blueCard)"]
]);

export interface ParseRichTextOptions {
	/**
	 * Trusted log lines only: enables named palette colours ([c=red]) and a
	 * backslash escape (\[ \* \\) so interpolated player names stay literal.
	 */
	allowLogTags?: boolean;
	/** Whether keyword markup [k=id]...[/k] should be parsed into interactive keyword segments. */
	allowKeywords?: boolean;
}

export interface RichSegment {
	text: string;
	bold?: true;
	italic?: true;
	color?: string;
	effect?: RichEffect;
	keyword?: string;
}

type Token =
	| { kind: "text"; value: string }
	| { kind: "bold" }
	| { kind: "italic" }
	| { kind: "openColor"; value: string; source?: string }
	| { kind: "closeColor" }
	| { kind: "openFx"; value: string }
	| { kind: "closeFx" }
	| { kind: "openKeyword"; value: string }
	| { kind: "closeKeyword" };

const TOKEN_RE =
	/(\*\*)|(\*)|\[c=([^\]]+)\]|\[\/c\]|\[fx=([^\]]+)\]|\[\/fx\]|\[k=([^\]]+)\]|\[\/k\]/g;

const ESCAPE_TOKEN_RE = new RegExp(`${TOKEN_RE.source}|\\${LOG_ESCAPE_CHAR}([\\\\\\[*])`, "g");

function tokenize(input: string, options: ParseRichTextOptions = {}): Token[] {
	const tokens: Token[] = [];
	let lastIndex = 0;
	let match: RegExpExecArray | null;

	const tokenRe = options.allowLogTags ? ESCAPE_TOKEN_RE : TOKEN_RE;
	tokenRe.lastIndex = 0;
	while ((match = tokenRe.exec(input)) !== null) {
		if (match.index > lastIndex) {
			tokens.push({ kind: "text", value: input.slice(lastIndex, match.index) });
		}

		const [full, bold, italic, colorValue, fxValue, keywordValue, escapedChar] = match;
		if (escapedChar !== undefined) tokens.push({ kind: "text", value: escapedChar });
		else if (bold) tokens.push({ kind: "bold" });
		else if (italic) tokens.push({ kind: "italic" });
		else if (colorValue !== undefined) {
			// Interpolated straight into a CSS `style` attribute by RichText/
			// TextEffects, so an unvalidated value here is a style-injection
			// vector once chat carries real, other-user-authored text:
			// reject anything that isn't a plain hex color, degrading to
			// literal text like any other malformed tag.
			const cssColor = HEX_COLOR_RE.test(colorValue)
				? colorValue
				: options.allowLogTags
					? LOG_COLOR_VARS.get(colorValue)
					: undefined;
			tokens.push(
				cssColor
					? { kind: "openColor", value: cssColor, source: colorValue }
					: { kind: "text", value: full }
			);
		} else if (full === "[/c]") tokens.push({ kind: "closeColor" });
		else if (fxValue !== undefined) {
			tokens.push(
				RICH_EFFECTS.includes(fxValue as RichEffect)
					? { kind: "openFx", value: fxValue }
					: { kind: "text", value: full }
			);
		} else if (full === "[/fx]") tokens.push({ kind: "closeFx" });
		else if (keywordValue !== undefined) {
			if (options.allowKeywords && KEYWORD_ID_RE.test(keywordValue)) {
				tokens.push({ kind: "openKeyword", value: keywordValue });
			} else {
				tokens.push({ kind: "text", value: full });
			}
		} else if (full === "[/k]") {
			if (options.allowKeywords) {
				tokens.push({ kind: "closeKeyword" });
			} else {
				tokens.push({ kind: "text", value: full });
			}
		}

		lastIndex = tokenRe.lastIndex;
	}
	if (lastIndex < input.length) {
		tokens.push({ kind: "text", value: input.slice(lastIndex) });
	}
	return tokens;
}

/** Literal source text a token would have consumed, for when it's demoted. */
function literalOf(token: Token): string {
	switch (token.kind) {
		case "bold":
			return "**";
		case "italic":
			return "*";
		case "openColor":
			return `[c=${token.source ?? token.value}]`;
		case "closeColor":
			return "[/c]";
		case "openFx":
			return `[fx=${token.value}]`;
		case "closeFx":
			return "[/fx]";
		case "openKeyword":
			return `[k=${token.value}]`;
		case "closeKeyword":
			return "[/k]";
		default:
			return token.value;
	}
}

/** Marks unmatched toggle tokens (bold/italic) as plain text in-place. */
function demoteUnmatchedToggles(tokens: Token[], kind: "bold" | "italic"): void {
	const indices = tokens.reduce<number[]>((acc, t, i) => {
		if (t.kind === kind) acc.push(i);
		return acc;
	}, []);
	if (indices.length % 2 === 1) {
		const strayIndex = indices[indices.length - 1];
		tokens[strayIndex] = { kind: "text", value: literalOf(tokens[strayIndex]) };
	}
}

/** Marks unmatched open/close pairs (color/fx/keyword) as plain text in-place. */
function demoteUnmatchedTagPairs(
	tokens: Token[],
	openKind: "openColor" | "openFx" | "openKeyword",
	closeKind: "closeColor" | "closeFx" | "closeKeyword"
): void {
	const openStack: number[] = [];
	for (let i = 0; i < tokens.length; i++) {
		const token = tokens[i];
		if (token.kind === openKind) {
			openStack.push(i);
		} else if (token.kind === closeKind) {
			if (openStack.length > 0) {
				openStack.pop();
			} else {
				tokens[i] = { kind: "text", value: literalOf(token) };
			}
		}
	}
	for (const strayOpenIndex of openStack) {
		tokens[strayOpenIndex] = { kind: "text", value: literalOf(tokens[strayOpenIndex]) };
	}
}

export function parseRichText(input: string, options: ParseRichTextOptions = {}): RichSegment[] {
	if (!input) return [];

	const tokens = tokenize(input, options);
	demoteUnmatchedToggles(tokens, "bold");
	demoteUnmatchedToggles(tokens, "italic");
	demoteUnmatchedTagPairs(tokens, "openColor", "closeColor");
	demoteUnmatchedTagPairs(tokens, "openFx", "closeFx");
	if (options.allowKeywords) {
		demoteUnmatchedTagPairs(tokens, "openKeyword", "closeKeyword");
	}

	const segments: RichSegment[] = [];
	let buffer = "";
	let bold = false;
	let italic = false;
	const colorStack: string[] = [];
	const fxStack: RichEffect[] = [];
	const keywordStack: string[] = [];

	const flush = () => {
		if (!buffer) return;
		const segment: RichSegment = { text: buffer };
		if (bold) segment.bold = true;
		if (italic) segment.italic = true;
		if (colorStack.length) segment.color = colorStack[colorStack.length - 1];
		if (fxStack.length) segment.effect = fxStack[fxStack.length - 1];
		if (keywordStack.length) segment.keyword = keywordStack[keywordStack.length - 1];
		segments.push(segment);
		buffer = "";
	};

	for (const token of tokens) {
		switch (token.kind) {
			case "text":
				buffer += token.value;
				break;
			case "bold":
				flush();
				bold = !bold;
				break;
			case "italic":
				flush();
				italic = !italic;
				break;
			case "openColor":
				flush();
				colorStack.push(token.value);
				break;
			case "closeColor":
				flush();
				colorStack.pop();
				break;
			case "openFx":
				flush();
				fxStack.push(token.value as RichEffect);
				break;
			case "closeFx":
				flush();
				fxStack.pop();
				break;
			case "openKeyword":
				flush();
				keywordStack.push(token.value);
				break;
			case "closeKeyword":
				flush();
				keywordStack.pop();
				break;
		}
	}
	flush();

	return segments;
}
