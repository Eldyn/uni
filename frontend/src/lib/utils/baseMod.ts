const BASE_MOD_ID = "uni";
const LEGACY_BASE_MOD_ID = "vanilla";

export const DRAW_DEBT_STATUS_KINDS: readonly string[] = [
	`${LEGACY_BASE_MOD_ID}:draw_debt`,
	`${BASE_MOD_ID}:draw_debt`
];

export function isBaseModNamespace(namespace: string): boolean {
	return namespace === BASE_MOD_ID || namespace === LEGACY_BASE_MOD_ID;
}

/** Rewrites a `uni:` kind id to its `vanilla:` form; other ids pass through. */
export function toLegacyBaseKindId(kindId: string): string {
	const prefix = `${BASE_MOD_ID}:`;
	if (!kindId.startsWith(prefix)) return kindId;
	return `${LEGACY_BASE_MOD_ID}:${kindId.slice(prefix.length)}`;
}
