import { describe, expect, it } from "vitest";
import { selectReactions, type ReactionRule } from "../../chat/playLog/reactionRules";
import type { ReactionContext } from "../../chat/playLog/reactionContext";
import { REACTION_RULES } from "../../chat/playLog/logBuilder";
import type { LogEvent } from "../../chat/playLog/logEvent";

const base: ReactionContext = {
	seq: 9,
	event: { kind: "reverse", seq: 9 },
	skipRun: 0,
	reverseRun: 0,
	drawRun: 0,
	totalDraws: 0,
	wildRun: 0,
	lastWildColor: null,
	prevWildColor: null,
	nearWinTarget: null,
	nearWinOpen: false
};

const rule: ReactionRule = {
	id: "x",
	match: () => true,
	group: (ctx) => (ctx.reverseRun >= 2 ? "two" : "one"),
	pools: { one: [{ key: "a" }], two: [{ key: "b" }, { key: "c" }] },
	params: () => ({})
};

describe("selectReactions", () => {
	it("returns one entry per matching rule", () => {
		const both: ReactionRule[] = [rule, { ...rule, id: "y", pools: { one: [{ key: "d" }] } }];
		expect(selectReactions(base, both).map((r) => r.key)).toEqual(["a", "d"]);
	});

	it("picks deterministically within a pool", () => {
		const a = selectReactions({ ...base, reverseRun: 2 }, [rule]);
		const b = selectReactions({ ...base, reverseRun: 2 }, [rule]);
		expect(a[0].key).toBe(b[0].key);
		expect(["b", "c"]).toContain(a[0].key);
	});

	it("skips rules whose match is false", () => {
		expect(selectReactions(base, [{ ...rule, match: () => false }])).toEqual([]);
	});
});

const kinds: LogEvent["kind"][] = [
	"play",
	"auto_play",
	"skip",
	"reverse",
	"draw",
	"reshuffle",
	"wild",
	"near_win",
	"win",
	"elimination"
];

it("every event kind is matched by at least one rule", () => {
	for (const kind of kinds) {
		expect(REACTION_RULES.some((r) => r.match({ ...base, event: { kind } as LogEvent }))).toBe(
			true
		);
	}
});
