/**
 * @file fxBudget.svelte.ts
 * @brief Reactive runtime FX budget/caps for the mod SFX system.
 * Values come from build-time Vite env (`import.meta.env`) with the documented
 * fallbacks and are publicly writable, so a runtime/ops path can override them
 * mid-session. Nothing is persisted — budget values are runtime + build env
 * only.
 *
 * `DEFAULT_FX_BUDGET` is the single source of truth for every budget default:
 * the entity runtime, the affliction manager/composition caps and this store
 * all read it, so a change lands in one place.
 */

/** The documented budget baselines. */
export const DEFAULT_FX_BUDGET = {
	maxEntities: 64,
	maxAfflictions: 8,
	screenStackMax: 3,
	stepTimeoutMs: 8000,
	queueMax: 64
} as const;

/** A cap must never be zero/negative: a floor of 1 keeps an override from
 *  silently disabling a budget. */
export const MIN_FX_BUDGET = 1;

/** Coerces one raw env value into a usable cap: blank/absent/non-finite falls
 *  back to the default; anything else is floored to an integer and clamped to
 *  `[MIN_FX_BUDGET, ∞)`. */
export function clampEnvInt(raw: unknown, fallback: number): number {
	if (raw === undefined || raw === null || raw === "") return fallback;
	const parsed = Number(raw);
	if (!Number.isFinite(parsed)) return fallback;
	return Math.max(MIN_FX_BUDGET, Math.floor(parsed));
}

/** Reads an integer env override. Prefers the `VITE_`-prefixed key and falls
 *  back to the bare `UNI_FX_*` name (exposed to the client because
 *  `vite.config.js` lists `UNI_FX_` in `envPrefix`), then the compile-time
 *  default. Malformed/zero/negative overrides are clamped so they can never
 *  disable a cap. */
export function readEnvInt(bareName: string, fallback: number): number {
	try {
		const env = import.meta.env as unknown as Record<string, unknown>;
		const raw = env[`VITE_${bareName}`] ?? env[bareName];
		return clampEnvInt(raw, fallback);
	} catch {
		return fallback;
	}
}

class StoreFxBudget {
	maxEntities = $state<number>(readEnvInt("UNI_FX_MAX_ENTITIES", DEFAULT_FX_BUDGET.maxEntities));
	maxAfflictions = $state<number>(
		readEnvInt("UNI_FX_MAX_AFFLICTIONS", DEFAULT_FX_BUDGET.maxAfflictions)
	);
	screenStackMax = $state<number>(
		readEnvInt("UNI_FX_SCREEN_STACK_MAX", DEFAULT_FX_BUDGET.screenStackMax)
	);
	stepTimeoutMs = $state<number>(
		readEnvInt("UNI_FX_STEP_TIMEOUT_MS", DEFAULT_FX_BUDGET.stepTimeoutMs)
	);
	queueMax = $state<number>(readEnvInt("UNI_FX_QUEUE_MAX", DEFAULT_FX_BUDGET.queueMax));
}

export const storeFxBudget = new StoreFxBudget();
