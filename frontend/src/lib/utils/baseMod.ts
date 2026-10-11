const BASE_MOD_ID = "uni";

export function baseKindId(localId: string): string {
	return `${BASE_MOD_ID}:${localId}`;
}

export const WILD_KIND_IDS: readonly string[] = [baseKindId("wild"), baseKindId("wild_draw4")];

export const CLASSIC_DECK_ID = baseKindId("classic");

export const DRAW_DEBT_STATUS_KIND = baseKindId("draw_debt");

export function isBaseModNamespace(namespace: string): boolean {
	return namespace === BASE_MOD_ID;
}
