/**
 * @file Lightweight entity-component runtime — component registry and core
 * types.
 *
 * Pure `.ts`, no runes: the runtime is a cosmetic replay subsystem and never
 * touches game state. Unknown component names are tolerated no-ops, never
 * throws, so a mod-authored def degrades instead of wedging the queue.
 */

/** A world-space position/tuple. */
export type Vec3 = [number, number, number];

/** Mutable per-entity runtime state, produced by component `create`/systems. */
export type EntityState = Record<string, unknown>;

/** One live entity instance. `components` is the declared (frozen) def params;
 *  `state` is the mutable bag systems read and patch. */
export interface Entity {
	instanceId: number;
	defId: string;
	handle?: string;
	components: Record<string, unknown>;
	state: Record<string, unknown>;
}

/** A registered component kind. `create` seeds initial state on spawn. */
export type ComponentKind = {
	family: string;
	create(params: unknown, e: Entity): EntityState;
};

/** A pure system: read entity + dt + params, return a state patch. */
export type SystemFn = (entity: Entity, dt: number, params: unknown) => EntityState;

/**
 * engine-owned component → family table (22 components). One entity
 * may declare at most one component per family; the server enforces this
 * fail-closed, the client only uses it to classify systems.
 */
export const COMPONENT_FAMILIES: Record<string, string> = {
	"visual.model": "visual",
	"visual.shader": "visual",
	"visual.sprite": "visual",
	"visual.emoji": "visual",
	"movement.arc": "movement",
	"movement.linear": "movement",
	"movement.ballistic": "movement",
	"movement.static": "movement",
	"motion.oscillate": "motion",
	"motion.jitter": "motion",
	"motion.spin": "motion",
	"motion.spring": "motion",
	"motion.follow": "motion",
	"emitter.particles": "emitter",
	"audio.sound": "audio",
	"outcome.resolver": "outcome",
	"camera.shake": "camera",
	"camera.zoom": "camera",
	"camera.punch": "camera",
	"screen.effect": "screen",
	"lifetime.ttl": "lifetime",
	"lifetime.untilDespawn": "lifetime"
};

/**
 * @brief Maps component names to their kind + system. Unknown names resolve to
 * a tolerated no-op kind and a warning (never a throw).
 */
export class ComponentRegistry {
	static families: Record<string, string> = COMPONENT_FAMILIES;

	#kinds = new Map<string, ComponentKind>();
	#systems = new Map<string, SystemFn>();

	register(name: string, kind: ComponentKind): void {
		this.#kinds.set(name, kind);
	}

	registerSystem(name: string, fn: SystemFn): void {
		this.#systems.set(name, fn);
	}

	/** The kind for `name`, or a warned no-op for an unknown component. */
	get(name: string): ComponentKind {
		const kind = this.#kinds.get(name);
		if (kind) return kind;
		console.warn(`[entity] unknown component "${name}" — tolerated no-op`);
		return { family: "unknown", create: () => ({}) };
	}

	/** The system driving `name`, if one is registered. */
	getSystem(name: string): SystemFn | undefined {
		return this.#systems.get(name);
	}

	has(name: string): boolean {
		return this.#kinds.has(name);
	}
}
