/**
 * @file richText.ts
 * @brief Parses a small inline markup grammar into flat, renderable segments.
 *
 * Grammar: `**bold**`, `*italic*`, `[c=value]...[/c]` (color),
 * `[fx=kind[:intensity[,speed]]]...[/fx]` (effect, one of TextEffects' effect
 * names; intensity/speed are integers 0-3, default 1). Tags nest. No underline
 * or strikethrough tokens exist by design.
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
const KEYWORD_ID_RE = /^[a-z0-9_-]+(:[a-z0-9_-]+)?$/i;

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
	effectIntensity?: number;
	effectSpeed?: number;
	keyword?: string;
}

type Token =
	| { kind: "text"; value: string }
	| { kind: "bold" }
	| { kind: "italic" }
	| { kind: "openColor"; value: string; source?: string }
	| { kind: "closeColor" }
	| { kind: "openFx"; value: RichEffect; intensity: number; speed: number; source: string }
	| { kind: "closeFx" }
	| { kind: "openKeyword"; value: string }
	| { kind: "closeKeyword" };

/**
 * Parses the params half of an `[fx=kind[:intensity[,speed]]]` tag. Returns
 * null for an unknown kind or any malformed param shape (a speed without an
 * intensity, non-integer values, too many parts). Values are clamped to 0-3;
 * params are numeric-only, so there is no style-injection surface.
 */
function parseFxParams(raw: string): { kind: RichEffect; intensity: number; speed: number } | null {
	const [kind, params] = raw.split(":");
	if (!RICH_EFFECTS.includes(kind as RichEffect)) return null;
	if (params === undefined) return { kind: kind as RichEffect, intensity: 1, speed: 1 };
	const parts = params.split(",");
	if (parts.length > 2 || parts[0] === "") return null;
	const clamp = (n: number) => Math.max(0, Math.min(3, n));
	const intensity = Number(parts[0]);
	if (!Number.isInteger(intensity)) return null;
	if (parts.length === 2) {
		const speed = Number(parts[1]);
		if (!Number.isInteger(speed)) return null;
		return { kind: kind as RichEffect, intensity: clamp(intensity), speed: clamp(speed) };
	}
	return { kind: kind as RichEffect, intensity: clamp(intensity), speed: 1 };
}

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
			const parsed = parseFxParams(fxValue);
			tokens.push(
				parsed
					? {
							kind: "openFx",
							value: parsed.kind,
							intensity: parsed.intensity,
							speed: parsed.speed,
							source: fxValue
						}
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
			return `[fx=${token.source}]`;
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
	const fxStack: { value: RichEffect; intensity: number; speed: number; source: string }[] = [];
	const keywordStack: string[] = [];

	const flush = () => {
		if (!buffer) return;
		const segment: RichSegment = { text: buffer };
		if (bold) segment.bold = true;
		if (italic) segment.italic = true;
		if (colorStack.length) segment.color = colorStack[colorStack.length - 1];
		if (fxStack.length) {
			const fx = fxStack[fxStack.length - 1];
			segment.effect = fx.value;
			// Only surface params that were spelled out; a bare [fx=kind] keeps
			// its segment shape unchanged so existing callers/tests don't shift.
			if (fx.source.includes(":")) {
				segment.effectIntensity = fx.intensity;
				segment.effectSpeed = fx.speed;
			}
		}
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
				fxStack.push({
					value: token.value,
					intensity: token.intensity,
					speed: token.speed,
					source: token.source
				});
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

/**
 * Maps an effect's parsed intensity/speed (0-3, default 1) onto TextEffects'
 * per-effect tuning props. Index 1 reproduces the component's own defaults, so
 * a bare `[fx=kind]` renders exactly as before.
 */
export function fxRenderProps(effect: RichEffect, intensity = 1, speed = 1) {
	switch (effect) {
		case "shake":
			return {
				shakeIntensity: [0, 3, 5, 8][intensity],
				shakeSpeed: [0.6, 0.4, 0.28, 0.18][speed]
			};
		case "undulate":
			return { amplitude: [0, 10, 16, 24][intensity], speed: [1.6, 1.2, 0.85, 0.55][speed] };
		case "shine":
			return { shineSpeed: [3.4, 2.5, 1.7, 1.0][speed] };
	}
}
