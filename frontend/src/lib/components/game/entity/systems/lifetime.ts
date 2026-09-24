/**
 * @file Lifetime systems: `lifetime.ttl` / `lifetime.untilDespawn`.
 * Pure `(entity, dt, params) => statePatch`. `ttl` marks `state.expired`, which
 * the runtime despawns after the tick; `untilDespawn` only accumulates age.
 */
import type { EntityState, SystemFn } from "../registry";
import { readNumber } from "./util";

export const lifetimeTtl: SystemFn = (e, dt, params): EntityState => {
	const p = (params ?? {}) as { seconds?: number };
	const age = readNumber(e.state.age, 0) + dt;
	const seconds = readNumber(p.seconds, Number.POSITIVE_INFINITY);
	return { age, expired: age >= seconds };
};

export const lifetimeUntilDespawn: SystemFn = (e, dt): EntityState => ({
	age: readNumber(e.state.age, 0) + dt
});

export const lifetimeSystems: Record<string, SystemFn> = {
	"lifetime.ttl": lifetimeTtl,
	"lifetime.untilDespawn": lifetimeUntilDespawn
};
