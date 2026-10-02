import { stableIndex } from "./stableIndex";
import type { ReactionContext } from "./reactionContext";

export type ReactionVariant = { key: string; when?(ctx: ReactionContext): boolean };

export type ReactionRule = {
	id: string;
	match(ctx: ReactionContext): boolean;
	group(ctx: ReactionContext): string;
	pools: Record<string, ReactionVariant[]>;
	params(ctx: ReactionContext): Record<string, string | number>;
};

export type SelectedReaction = {
	ruleId: string;
	key: string;
	params: Record<string, string | number>;
};

export function selectReactions(ctx: ReactionContext, rules: ReactionRule[]): SelectedReaction[] {
	const out: SelectedReaction[] = [];
	for (const rule of rules) {
		if (!rule.match(ctx)) continue;
		const group = rule.group(ctx);
		const pool = rule.pools[group] ?? rule.pools.generic ?? [];
		const eligible = pool.filter((v) => !v.when || v.when(ctx));
		const pickFrom = eligible.length > 0 ? eligible : pool;
		if (pickFrom.length === 0) continue;
		const index = stableIndex(ctx.seq, `${rule.id}:${group}`, pickFrom.length);
		out.push({ ruleId: rule.id, key: pickFrom[index].key, params: rule.params(ctx) });
	}
	return out;
}
