/**
 * @file impactProfile.ts
 * @brief How hard a special card hits the table when it lands: a short
 * hit-stop and a camera punch toward the table.
 *
 * Every tuning value lives in IMPACT_TUNING below, so the feel can be adjusted
 * in one place. Cards are matched by their wire `value` (generated ValueMap);
 * anything else, including mod-defined values, gets no impact.
 */

export interface ImpactProfile {
	/** Real-time freeze on landing, milliseconds. Never above MAX_HIT_STOP_MS. */
	hitStopMs: number;
	/** Camera dolly toward the table at the punch peak, world units. */
	cameraPunch: number;
}

/** Hard cap on any hit-stop, after the speed multiplier is applied. */
export const MAX_HIT_STOP_MS = 120;

export const IMPACT_TUNING = {
	/** Timeline speed while a hit-stop holds the landing beat. */
	hitStopTimeScale: 0.05,
	/** Punch in, then settle back to the rig's base pose. The whole punch
	 *  fits inside the landing shake, so it never lengthens its beat. */
	punchAttackS: 0.04,
	punchReleaseS: 0.09,
	/** Per card value (generated ValueMap ids). Conservative defaults. */
	byValue: {
		skip: { hitStopMs: 40, cameraPunch: 0.12 },
		reverse: { hitStopMs: 40, cameraPunch: 0.12 },
		"+2": { hitStopMs: 60, cameraPunch: 0.18 },
		jolly: { hitStopMs: 60, cameraPunch: 0.18 },
		jolly_draw4: { hitStopMs: 90, cameraPunch: 0.3 }
	}
} as const;

const IMPACT_BY_VALUE = new Map<string, { hitStopMs: number; cameraPunch: number }>(
	Object.entries(IMPACT_TUNING.byValue)
);

/** Impact for a landed card, or null for a plain number or unknown card. */
export function impactProfileFor(card: { type: string; value: string }): ImpactProfile | null {
	const impact = IMPACT_BY_VALUE.get(card.value);
	if (!impact) return null;
	return {
		hitStopMs: Math.min(MAX_HIT_STOP_MS, impact.hitStopMs),
		cameraPunch: impact.cameraPunch
	};
}
