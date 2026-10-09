/**
 * @file messageFormat.ts
 * @brief The formatter for mod texts.
 *
 * Grammar:
 *  - `{name}`: the value of a parameter.
 *  - `{name, plural, one {# card} other {# cards}}`: a plural. The category
 *    comes from `Intl.PluralRules` (zero, one, two, few, many, other). Each
 *    branch is a message. `#` is the number. `other` is required. An exact
 *    match (`=0`), gender, and `select` are not supported.
 *  - `\{`, `\}`, and `\#` give a literal character.
 *  - A backslash before any other character stays, with that character, for
 *    the rich text parser (`\[`, `\*`, `\\`).
 *  - The rich text tags `[c=...]`, `[fx=...]`, and `[k=...]` are plain text
 *    here. The output keeps them and `logRichText` parses them.
 *
 * The formatter never throws. A missing parameter leaves its placeholder in the
 * text and gives a warning. A malformed pattern returns the raw pattern and
 * gives an error.
 */

export interface FormatReporter {
	warn(message: string): void;
	error(message: string): void;
}

export type FormatParams = Record<string, string | number>;

export interface FormatOptions {
	/** The locale of the pattern, for the plural rules. */
	locale: string;
	report?: FormatReporter;
	/** Applied to each literal text piece (the pseudo locale uses it). */
	transformText?: (text: string) => string;
}

type PluralCategory = "zero" | "one" | "two" | "few" | "many" | "other";

type Node =
	| { kind: "text"; value: string }
	| { kind: "argument"; name: string; source: string }
	| { kind: "number" }
	| { kind: "plural"; name: string; branches: Map<PluralCategory, Node[]>; source: string };

const PLURAL_CATEGORIES: ReadonlySet<string> = new Set([
	"zero",
	"one",
	"two",
	"few",
	"many",
	"other"
]);
const ESCAPABLE_CHARS: ReadonlySet<string> = new Set(["{", "}", "#"]);
const IDENTIFIER_START = /[A-Za-z_]/;
const IDENTIFIER_PART = /[A-Za-z0-9_]/;
const WORD_PART = /[a-z]/;
const MAX_NESTING = 8;
const PARSE_CACHE_LIMIT = 500;
const ESCAPE_CHAR = "\\";

const defaultReporter: FormatReporter = {
	warn: (message) => console.warn(message),
	error: (message) => console.error(message)
};

class PatternError extends Error {}

class Parser {
	#pattern: string;
	#index = 0;

	constructor(pattern: string) {
		this.#pattern = pattern;
	}

	parse(): Node[] {
		const nodes = this.#message(0, false);
		if (this.#index < this.#pattern.length) {
			throw new PatternError(`unexpected "}" at ${this.#index}`);
		}
		return nodes;
	}

	#message(depth: number, inPlural: boolean): Node[] {
		if (depth > MAX_NESTING) throw new PatternError("the pattern nests too deep");
		const nodes: Node[] = [];
		let text = "";
		const flush = () => {
			if (text !== "") nodes.push({ kind: "text", value: text });
			text = "";
		};
		while (this.#index < this.#pattern.length) {
			const char = this.#pattern[this.#index]!;
			if (char === "}") break;
			if (char === ESCAPE_CHAR) {
				const next = this.#pattern[this.#index + 1];
				if (next !== undefined && ESCAPABLE_CHARS.has(next)) {
					text += next;
				} else {
					text += next === undefined ? ESCAPE_CHAR : ESCAPE_CHAR + next;
				}
				this.#index += 2;
				continue;
			}
			if (char === "{") {
				flush();
				nodes.push(this.#argument(depth));
				continue;
			}
			if (char === "#" && inPlural) {
				flush();
				nodes.push({ kind: "number" });
			} else {
				text += char;
			}
			this.#index += 1;
		}
		flush();
		return nodes;
	}

	#argument(depth: number): Node {
		const start = this.#index;
		this.#index += 1;
		this.#skipSpace();
		const name = this.#identifier();
		this.#skipSpace();
		if (this.#take("}")) {
			return { kind: "argument", name, source: this.#pattern.slice(start, this.#index) };
		}
		this.#expect(",");
		this.#skipSpace();
		if (this.#word() !== "plural") throw new PatternError(`only "plural" is supported`);
		this.#skipSpace();
		this.#expect(",");
		const branches = new Map<PluralCategory, Node[]>();
		for (;;) {
			this.#skipSpace();
			if (this.#take("}")) break;
			const category = this.#word();
			if (!PLURAL_CATEGORIES.has(category)) {
				throw new PatternError(`unknown plural category "${category}"`);
			}
			if (branches.has(category as PluralCategory)) {
				throw new PatternError(`duplicate plural category "${category}"`);
			}
			this.#skipSpace();
			this.#expect("{");
			branches.set(category as PluralCategory, this.#message(depth + 1, true));
			this.#expect("}");
		}
		if (!branches.has("other")) throw new PatternError(`a plural needs an "other" branch`);
		return { kind: "plural", name, branches, source: this.#pattern.slice(start, this.#index) };
	}

	#skipSpace(): void {
		while (/\s/.test(this.#pattern[this.#index] ?? "x")) this.#index += 1;
	}

	#take(char: string): boolean {
		if (this.#pattern[this.#index] !== char) return false;
		this.#index += 1;
		return true;
	}

	#expect(char: string): void {
		if (!this.#take(char)) throw new PatternError(`expected "${char}" at ${this.#index}`);
	}

	#identifier(): string {
		const start = this.#index;
		if (!IDENTIFIER_START.test(this.#pattern[this.#index] ?? "")) {
			throw new PatternError(`expected a name at ${this.#index}`);
		}
		while (IDENTIFIER_PART.test(this.#pattern[this.#index] ?? "")) this.#index += 1;
		return this.#pattern.slice(start, this.#index);
	}

	#word(): string {
		const start = this.#index;
		while (WORD_PART.test(this.#pattern[this.#index] ?? "")) this.#index += 1;
		if (start === this.#index) throw new PatternError(`expected a word at ${this.#index}`);
		return this.#pattern.slice(start, this.#index);
	}
}

const parseCache = new Map<string, Node[] | PatternError>();

function parsePattern(pattern: string): Node[] | PatternError {
	const cached = parseCache.get(pattern);
	if (cached !== undefined) return cached;
	let result: Node[] | PatternError;
	try {
		result = new Parser(pattern).parse();
	} catch (error) {
		result = error instanceof PatternError ? error : new PatternError(String(error));
	}
	if (parseCache.size >= PARSE_CACHE_LIMIT) parseCache.clear();
	parseCache.set(pattern, result);
	return result;
}

const pluralRulesByLocale = new Map<string, Intl.PluralRules>();

function pluralRulesFor(locale: string): Intl.PluralRules {
	let rules = pluralRulesByLocale.get(locale);
	if (rules === undefined) {
		try {
			rules = new Intl.PluralRules(locale);
		} catch {
			rules = new Intl.PluralRules("en");
		}
		pluralRulesByLocale.set(locale, rules);
	}
	return rules;
}

function numberOf(value: string | number | undefined): number | null {
	if (value === undefined) return null;
	const number = typeof value === "number" ? value : value.trim() === "" ? NaN : Number(value);
	return Number.isFinite(number) ? number : null;
}

/**
 * INFO: a parameter value is inserted as written. The caller escapes values
 * that come from players (`escapeLogText`), so the formatter never escapes
 * twice and a name cannot open a rich text tag.
 */
function render(
	nodes: Node[],
	params: FormatParams,
	options: FormatOptions,
	report: FormatReporter,
	currentNumber: number | null
): string {
	const transform = options.transformText ?? ((text: string) => text);
	let output = "";
	for (const node of nodes) {
		if (node.kind === "text") {
			output += transform(node.value);
		} else if (node.kind === "number") {
			output += currentNumber === null ? "#" : String(currentNumber);
		} else if (node.kind === "argument") {
			const value = params[node.name];
			if (value === undefined) {
				report.warn(`Missing text parameter "${node.name}".`);
				output += node.source;
			} else {
				output += String(value);
			}
		} else {
			const count = numberOf(params[node.name]);
			if (count === null) {
				report.warn(`Missing or non-numeric plural parameter "${node.name}".`);
				output += node.source;
				continue;
			}
			const category = pluralRulesFor(options.locale).select(count);
			const branch = node.branches.get(category) ?? node.branches.get("other")!;
			output += render(branch, params, options, report, count);
		}
	}
	return output;
}

/** Formats one pattern. Never throws. */
export function formatMessage(
	pattern: string,
	params: FormatParams,
	options: FormatOptions
): string {
	const report = options.report ?? defaultReporter;
	try {
		const parsed = parsePattern(pattern);
		if (parsed instanceof PatternError) {
			report.error(`Malformed text pattern "${pattern}": ${parsed.message}.`);
			return pattern;
		}
		return render(parsed, params, options, report, null);
	} catch (error) {
		report.error(`Text pattern "${pattern}" failed: ${String(error)}.`);
		return pattern;
	}
}
