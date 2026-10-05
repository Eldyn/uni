/**
 * @file debtIntensity.ts
 * @brief Buckets the pending draw debt into an escalation tier shared by the
 * draw-stack badge and the draw pile's tremor and dust.
 */

export type DebtTier = 0 | 1 | 2 | 3;

const MEDIUM_DEBT_MIN = 4;
const HEAVY_DEBT_MIN = 8;

export function debtIntensity(pendingDraws: number): DebtTier {
	if (!Number.isFinite(pendingDraws) || pendingDraws <= 0) return 0;
	if (pendingDraws >= HEAVY_DEBT_MIN) return 3;
	if (pendingDraws >= MEDIUM_DEBT_MIN) return 2;
	return 1;
}

const TREMOR_AMPLITUDE_BY_TIER: readonly number[] = [0, 0.003, 0.006, 0.011];
const TREMOR_ANGULAR_SPEED = 62;

const DUST_MOTE_COUNT_BY_TIER: readonly number[] = [0, 3, 7, 12];
const MOTE_RISE_HEIGHT = 0.5;
const MOTE_SPREAD_X = 0.5;
const MOTE_SPREAD_Z = 0.6;
const MOTE_CYCLE_SECONDS = 1.6;
const MOTE_PHASE_STEP = 0.618;
const MOTE_MAX_OPACITY = 0.55;
/** Per-mote Z fan: angle step between motes and half the total spread. */
const MOTE_Z_ANGLE_STEP = 2.4;
const MOTE_Z_FAN_HALF = 0.5;

/** Horizontal shake applied to the draw pile's cards, 0 at tier 0. */
export function debtTremorOffset(tier: DebtTier, timeSeconds: number): number {
	return Math.sin(timeSeconds * TREMOR_ANGULAR_SPEED) * TREMOR_AMPLITUDE_BY_TIER[tier];
}

export function debtDustMoteCount(tier: DebtTier): number {
	return DUST_MOTE_COUNT_BY_TIER[tier];
}

export interface DebtMotePose {
	offsetX: number;
	offsetY: number;
	offsetZ: number;
	opacity: number;
}

/** Where mote `index` sits relative to the pile top: it rises and fades. */
export function debtMotePose(index: number, timeSeconds: number): DebtMotePose {
	const phase = (index * MOTE_PHASE_STEP) % 1;
	const progress = (timeSeconds / MOTE_CYCLE_SECONDS + phase) % 1;
	const lateral = (phase - 0.5) * 2;
	return {
		offsetX: lateral * MOTE_SPREAD_X,
		offsetY: progress * MOTE_RISE_HEIGHT,
		offsetZ: Math.cos(index * MOTE_Z_ANGLE_STEP) * MOTE_SPREAD_Z * MOTE_Z_FAN_HALF,
		opacity: Math.sin(progress * Math.PI) * MOTE_MAX_OPACITY
	};
}
