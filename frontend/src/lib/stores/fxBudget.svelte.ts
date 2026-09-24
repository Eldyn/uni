/**
 * @file fxBudget.svelte.ts
 * @brief Reactive runtime FX budget/caps for the mod SFX system.
 * Values come from build-time Vite env (`import.meta.env`) with the documented
 * fallbacks and are publicly writable, so a runtime/ops path can override them
 * mid-session. Nothing is persisted — budget values are runtime + build env
 * only.
 */

const DEFAULT_MAX_ENTITIES = 64;
const DEFAULT_MAX_AFFLICTIONS = 8;
const DEFAULT_SCREEN_STACK_MAX = 3;
const DEFAULT_STEP_TIMEOUT_MS = 8000;
const DEFAULT_QUEUE_MAX = 64;

/** Reads an integer env override. Prefers the `VITE_`-prefixed key (what Vite
 *  actually exposes to the client) and falls back to the documented bare name,
 *  then the compile-time default. Blank/non-finite overrides are ignored so a
 *  malformed value can never disable a cap. */
function readEnvInt(bareName: string, fallback: number): number {
	try {
		const env = import.meta.env as unknown as Record<string, unknown>;
		const raw = env[`VITE_${bareName}`] ?? env[bareName];
		if (raw === undefined || raw === null || raw === "") return fallback;
		const parsed = Number(raw);
		return Number.isFinite(parsed) ? parsed : fallback;
	} catch {
		return fallback;
	}
}

class StoreFxBudget {
	maxEntities = $state<number>(readEnvInt("UNI_FX_MAX_ENTITIES", DEFAULT_MAX_ENTITIES));
	maxAfflictions = $state<number>(readEnvInt("UNI_FX_MAX_AFFLICTIONS", DEFAULT_MAX_AFFLICTIONS));
	screenStackMax = $state<number>(readEnvInt("UNI_FX_SCREEN_STACK_MAX", DEFAULT_SCREEN_STACK_MAX));
	stepTimeoutMs = $state<number>(readEnvInt("UNI_FX_STEP_TIMEOUT_MS", DEFAULT_STEP_TIMEOUT_MS));
	queueMax = $state<number>(readEnvInt("UNI_FX_QUEUE_MAX", DEFAULT_QUEUE_MAX));
}

export const storeFxBudget = new StoreFxBudget();
