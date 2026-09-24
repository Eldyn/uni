/**
 * @file EntityRuntime — the client-side entity-component runtime.
 *
 * A cosmetic replay subsystem: it drives entity systems and a renderer but
 * never reads or writes game state. The client is tolerant — an unknown def,
 * unknown component, missing anchor, over-cap spawn or duplicate handle all
 * degrade with a warning and never throw.
 */
import { SvelteMap } from "svelte/reactivity";
import { COMPONENT_FAMILIES, ComponentRegistry, type Entity, type Vec3 } from "./registry";
import { moveSystems } from "./systems/move";
import { motionSystems } from "./systems/motion";
import { lifetimeSystems } from "./systems/lifetime";
import { noopEntityRenderer, type EntityRenderer } from "./entityRenderer";

/** Default cap; Task 6's `fxBudget` store injects its own via the constructor. */
export const DEFAULT_MAX_ENTITIES = 64;

const MOVE_ORDER = ["movement.static", "movement.linear", "movement.arc", "movement.ballistic"];
const MOTION_ORDER = [
	"motion.oscillate",
	"motion.jitter",
	"motion.spin",
	"motion.spring",
	"motion.follow"
];
const LIFETIME_ORDER = ["lifetime.untilDespawn", "lifetime.ttl"];

export interface EntityDef {
	components: Record<string, unknown>;
}

export interface EntityRuntimeOptions {
	maxEntities?: number;
	renderer?: EntityRenderer;
	registry?: ComponentRegistry;
}

/** A registry seeded with all component kinds plus the three system sets. */
function defaultRegistry(): ComponentRegistry {
	const registry = new ComponentRegistry();
	for (const [name, family] of Object.entries(COMPONENT_FAMILIES)) {
		registry.register(name, { family, create: () => ({}) });
	}
	for (const set of [moveSystems, motionSystems, lifetimeSystems]) {
		for (const [name, system] of Object.entries(set)) {
			registry.registerSystem(name, system);
		}
	}
	return registry;
}

export class EntityRuntime {
	#nextId = 1;
	/** Reactive map the renderer can observe (mirrors cardRegistry's
	 *  SvelteSet idiom); a no-op renderer only needs the callbacks. */
	#entities = new SvelteMap<number, Entity>();
	#handles = new Map<string, number>();
	/** Shared anchor map: anchor address -> entity instanceId, written by
	 *  `attachTo` and read when resolving movement `$origin`/`$target` refs. */
	#anchors = new Map<string, number>();
	#defs = new Map<string, EntityDef>();
	/** `${instanceId}|${anchor}` refs already warned about, so a missing anchor
	 *  warns once instead of every tick. */
	#warnedAnchors = new Set<string>();
	#maxEntities: number;
	#renderer: EntityRenderer;
	#registry: ComponentRegistry;

	constructor(options: EntityRuntimeOptions = {}) {
		this.#maxEntities = options.maxEntities ?? DEFAULT_MAX_ENTITIES;
		this.#renderer = options.renderer ?? noopEntityRenderer;
		this.#registry = options.registry ?? defaultRegistry();
	}

	/** Seed a def (the server `defs` packet feeds these). */
	define(defId: string, def: EntityDef): void {
		this.#defs.set(defId, def);
	}

	spawn(defId: string, opts: { as?: string } = {}): number {
		const def = this.#defs.get(defId);
		if (!def) {
			console.warn(`[entity] spawn of unknown def "${defId}" — dropped`);
			return 0;
		}
		if (this.#entities.size >= this.#maxEntities) {
			console.warn(
				`[entity] spawn dropped: entity cap (${this.#maxEntities}) reached — "${defId}" not spawned`
			);
			return 0;
		}

		const instanceId = this.#nextId++;
		const entity: Entity = {
			instanceId,
			defId,
			components: { ...def.components },
			state: {
				position: [0, 0, 0],
				offset: [0, 0, 0],
				velocity: [0, 0, 0],
				angle: 0,
				age: 0,
				expired: false,
				anchors: {}
			}
		};

		const handle = opts.as ? this.#uniqueHandle(opts.as) : String(instanceId);
		entity.handle = handle;
		this.#handles.set(handle, instanceId);

		for (const [name, params] of Object.entries(entity.components)) {
			Object.assign(entity.state, this.#registry.get(name).create(params, entity));
		}

		this.#entities.set(instanceId, entity);
		this.#renderer.mount(entity);
		return instanceId;
	}

	despawn(id: number): void {
		const entity = this.#entities.get(id);
		if (!entity) return;
		this.#entities.delete(id);
		if (entity.handle !== undefined) this.#handles.delete(entity.handle);
		for (const [anchor, anchoredId] of this.#anchors) {
			if (anchoredId === id) this.#anchors.delete(anchor);
		}
		for (const key of this.#warnedAnchors) {
			if (key.startsWith(`${id}|`)) this.#warnedAnchors.delete(key);
		}
		this.#renderer.unmount(id);
	}

	setState(id: number, patch: Record<string, unknown>): void {
		const entity = this.#entities.get(id);
		if (!entity) {
			console.warn(`[entity] setState on unknown entity ${id} — ignored`);
			return;
		}
		Object.assign(entity.state, patch);
	}

	/** Register the entity under an anchor address so movement `$origin` /
	 *  `$target` (and `motion.*.target`) refs resolve to its position. */
	attachTo(id: number, anchor: string): void {
		if (!this.#entities.has(id)) {
			console.warn(`[entity] attachTo unknown entity ${id} — ignored`);
			return;
		}
		this.#anchors.set(anchor, id);
	}

	resolve(address: string): Entity | undefined {
		if (address.startsWith("entity:")) {
			const id = this.#handles.get(address.slice("entity:".length));
			return id === undefined ? undefined : this.#entities.get(id);
		}
		const anchoredId = this.#anchors.get(address);
		return anchoredId === undefined ? undefined : this.#entities.get(anchoredId);
	}

	tick(deltaS: number): void {
		for (const entity of this.#entities.values()) {
			this.#resolveAnchors(entity);
			this.#applySystems(entity, deltaS, MOVE_ORDER);
			this.#applySystems(entity, deltaS, MOTION_ORDER);
			this.#applySystems(entity, deltaS, LIFETIME_ORDER);
			this.#renderer.update(entity, deltaS);
		}
		const expired: number[] = [];
		for (const entity of this.#entities.values()) {
			if (entity.state.expired === true) expired.push(entity.instanceId);
		}
		for (const id of expired) this.despawn(id);
	}

	get liveCount(): number {
		return this.#entities.size;
	}

	#uniqueHandle(requested: string): string {
		if (!this.#handles.has(requested)) return requested;
		let suffix = 2;
		while (this.#handles.has(`${requested}#${suffix}`)) suffix++;
		const handle = `${requested}#${suffix}`;
		console.warn(`[entity] handle "${requested}" in use — auto-suffixed to "${handle}"`);
		return handle;
	}

	#applySystems(entity: Entity, dt: number, order: string[]): void {
		for (const name of order) {
			const params = entity.components[name];
			if (params === undefined) continue;
			const system = this.#registry.getSystem(name);
			if (!system) continue;
			Object.assign(entity.state, system(entity, dt, params));
		}
	}

	/** Resolve every anchor address this entity references into concrete world
	 *  positions, stashing them on `state.anchors` for the pure systems. */
	#resolveAnchors(entity: Entity): void {
		const refs = new Set<string>();
		for (const name of ["movement.arc", "movement.linear", "movement.ballistic"]) {
			const params = entity.components[name] as { from?: string; to?: string } | undefined;
			if (params?.from) refs.add(params.from);
			if (params?.to) refs.add(params.to);
		}
		for (const name of ["motion.spring", "motion.follow"]) {
			const params = entity.components[name] as { target?: string } | undefined;
			if (params?.target) refs.add(params.target);
		}
		const anchors: Record<string, Vec3> = {};
		for (const ref of refs) {
			const position = this.#anchorPosition(ref);
			if (position) {
				anchors[ref] = position;
				continue;
			}
			const key = `${entity.instanceId}|${ref}`;
			if (this.#warnedAnchors.has(key)) continue;
			this.#warnedAnchors.add(key);
			console.warn(`[entity] anchor "${ref}" for entity ${entity.instanceId} not found — degraded`);
		}
		entity.state.anchors = anchors;
	}

	#anchorPosition(address: string): Vec3 | undefined {
		if (address.startsWith("entity:")) {
			const entity = this.resolve(address);
			return entity ? (entity.state.position as Vec3) : undefined;
		}
		const anchoredId = this.#anchors.get(address);
		if (anchoredId === undefined) return undefined;
		const entity = this.#entities.get(anchoredId);
		return entity ? (entity.state.position as Vec3) : undefined;
	}
}
