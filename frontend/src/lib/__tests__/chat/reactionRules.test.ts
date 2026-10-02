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

	it("uses when to filter the pool", () => {
		const gated: ReactionRule = {
			...rule,
			pools: { one: [{ key: "only", when: (ctx) => ctx.reverseRun === 5 }, { key: "fallback" }] }
		};
		expect(selectReactions(base, [gated])[0].key).toBe("fallback");
	});

	it("falls back to the generic pool when the group is unknown", () => {
		const g: ReactionRule = {
			...rule,
			group: () => "missing",
			pools: { generic: [{ key: "g" }] }
		};
		expect(selectReactions(base, [g])[0].key).toBe("g");
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

it("escalates wild-colour reactions by the consecutive run", () => {
	const wild = (n: number) =>
		REACTION_RULES.find((r) => r.id === "wild")!.group({
			...base,
			event: { kind: "wild", seq: 1, player: "Ann", color: "blue" },
			wildRun: n
		});
	expect(wild(1)).toBe("first");
	expect(wild(2)).toBe("second");
	expect(wild(3)).toBe("third");
});

it("offers an extra variant in each expanded pool", () => {
	const keysOf = (id: string, group: string) =>
		REACTION_RULES.find((r) => r.id === id)!.pools[group].map((v) => v.key);
	expect(keysOf("skip", "two")).toContain("log_skip_again_2");
	expect(keysOf("reverse", "two")).toContain("log_reverse_back_2");
	expect(keysOf("draw", "generic")).toContain("log_draw_generic_3");
	expect(keysOf("near_win", "generic")).toContain("log_near_win_3");
	expect(keysOf("reshuffle", "generic")).toContain("log_reshuffle_3");
});

it("every event kind is matched by at least one rule", () => {
	for (const kind of kinds) {
		expect(REACTION_RULES.some((r) => r.match({ ...base, event: { kind } as LogEvent }))).toBe(
			true
		);
	}
});

describe("blocked rule", () => {
	it("fires when the near-win target is skipped, naming the actor", () => {
		const blocked = REACTION_RULES.find((r) => r.id === "blocked")!;
		const ctx: ReactionContext = {
			...base,
			event: { kind: "skip", seq: 4, player: "Bob", actor: "Ann" },
			nearWinOpen: true,
			nearWinTarget: "Bob"
		};
		expect(blocked.match(ctx)).toBe(true);
		expect(blocked.params(ctx)).toEqual({ blocker: "Ann", target: "Bob" });
	});

	it("fires when the near-win target is penalty-drawn, naming the actor", () => {
		const blocked = REACTION_RULES.find((r) => r.id === "blocked")!;
		const ctx: ReactionContext = {
			...base,
			event: {
				kind: "draw",
				seq: 4,
				player: "Ann",
				victim: "Bob",
				count: 2,
				penalty: true,
				actor: "Ann"
			},
			nearWinOpen: true,
			nearWinTarget: "Bob"
		};
		expect(blocked.match(ctx)).toBe(true);
		expect(blocked.params(ctx)).toEqual({ blocker: "Ann", target: "Bob" });
	});

	it("does not fire when the window is closed", () => {
		const blocked = REACTION_RULES.find((r) => r.id === "blocked")!;
		const ctx: ReactionContext = {
			...base,
			event: { kind: "skip", seq: 4, player: "Bob", actor: "Ann" },
			nearWinOpen: false,
			nearWinTarget: "Bob"
		};
		expect(blocked.match(ctx)).toBe(false);
	});

	it("does not fire for a different target or a missing actor", () => {
		const blocked = REACTION_RULES.find((r) => r.id === "blocked")!;
		expect(
			blocked.match({
				...base,
				event: { kind: "skip", seq: 4, player: "Cy", actor: "Ann" },
				nearWinOpen: true,
				nearWinTarget: "Bob"
			})
		).toBe(false);
		expect(
			blocked.match({
				...base,
				event: { kind: "skip", seq: 4, player: "Bob" },
				nearWinOpen: true,
				nearWinTarget: "Bob"
			})
		).toBe(false);
	});
});
