import { describe, expect, it } from "vitest";
import { selectReactionKey, type ReactionRule } from "../../chat/playLog/reactionRules";

type E = { kind: "x"; seq: number; n: number };
const streak = { skipRun: 0, reverseRun: 0, stackedDebt: 0, drawRun: 0, totalDraws: 0 };

const rule: ReactionRule<E> = {
	group: (e) => (e.n >= 4 ? "many" : "few"),
	pools: {
		few: [{ key: "few_1" }, { key: "few_2" }],
		many: [{ key: "many_1" }, { key: "many_2" }]
	},
	params: () => ({})
};

describe("selectReactionKey", () => {
	it("picks from the group pool deterministically", () => {
		const a = selectReactionKey("x", rule, { kind: "x", seq: 9, n: 4 }, streak);
		const b = selectReactionKey("x", rule, { kind: "x", seq: 9, n: 4 }, streak);
		expect(a).toBe(b);
		expect(["many_1", "many_2"]).toContain(a);
	});

	it("uses when to filter the pool", () => {
		const gated: ReactionRule<E> = {
			...rule,
			pools: { many: [{ key: "only", when: (e) => e.n === 5 }, { key: "fallback" }] }
		};
		expect(selectReactionKey("x", gated, { kind: "x", seq: 1, n: 4 }, streak)).toBe("fallback");
	});

	it("falls back to the first pool entry when the filter empties it", () => {
		const empty: ReactionRule<E> = {
			...rule,
			pools: { many: [{ key: "never", when: () => false }] }
		};
		expect(selectReactionKey("x", empty, { kind: "x", seq: 1, n: 4 }, streak)).toBe("never");
	});

	it("falls back to generic when the group is unknown", () => {
		const g: ReactionRule<E> = {
			...rule,
			group: () => "missing",
			pools: { generic: [{ key: "g" }] }
		};
		expect(selectReactionKey("x", g, { kind: "x", seq: 1, n: 1 }, streak)).toBe("g");
	});
});
