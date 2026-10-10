/**
 * @file richTagProblems.ts
 * @brief Test support: the check of the rich text tags in copy. Every tag must
 * be known, resolvable, and closed in order.
 */

export const PALETTE_COLORS = ["red", "yellow", "green", "blue"];
const ALLOWED_EFFECTS = ["shake", "undulate", "shine"];
const HEX_COLOR_RE = /^#[0-9a-fA-F]{3,8}$/;
const TAG_RE = /\[(c|fx|k)=([^\]]*)\]|\[\/(c|fx|k)\]/g;

/** Returns a problem description for an [fx=...] value, or null when valid. */
function fxProblem(value: string): string | null {
	const [kind, params, ...extra] = value.split(":");
	if (!ALLOWED_EFFECTS.includes(kind)) return `unknown [fx=${value}]`;
	if (params === undefined) return null;
	if (extra.length > 0) return `malformed [fx=${value}]`;
	const parts = params.split(",");
	const isLevel = (raw: string | undefined) =>
		raw !== undefined && /^\d+$/.test(raw) && Number(raw) <= 3;
	if (parts.length > 2 || !isLevel(parts[0])) return `malformed [fx=${value}]`;
	if (parts.length === 2 && !isLevel(parts[1])) return `malformed [fx=${value}]`;
	return null;
}

/** Returns a problem description, or null when every tag is known, resolvable, and closed in order. */
export function tagProblem(text: string, isKnownKeyword: (id: string) => boolean): string | null {
	const open: string[] = [];
	for (const [, openName, value, closeName] of text.matchAll(TAG_RE)) {
		if (closeName) {
			if (open.pop() !== closeName) return `stray [/${closeName}]`;
			continue;
		}
		if (openName === "c") {
			if (!PALETTE_COLORS.includes(value) && !HEX_COLOR_RE.test(value))
				return `unknown [c=${value}]`;
		} else if (openName === "fx") {
			const problem = fxProblem(value);
			if (problem) return problem;
		} else if (openName === "k") {
			if (!isKnownKeyword(value)) return `unresolved [k=${value}]`;
		}
		open.push(openName);
	}
	return open.length > 0 ? `unclosed [${open.join(", ")}]` : null;
}
