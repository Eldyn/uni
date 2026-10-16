/**
 * @file Entity renderer seam. ships a no-op; swap in a
 * real Threlte-backed implementation. The runtime calls mount on spawn, update
 * each tick, unmount on despawn.
 */
import type { Entity } from "./registry";

export type { Entity };

export interface EntityRenderer {
	mount(e: Entity): void;
	update(e: Entity, dt: number): void;
	unmount(id: number): void;
}

/** entities are invisible in the runtime is fully testable without
 *  pixels. */
export class NoopEntityRenderer implements EntityRenderer {
	mount(_e: Entity): void {}
	update(_e: Entity, _dt: number): void {}
	unmount(_id: number): void {}
}

export const noopEntityRenderer = new NoopEntityRenderer();
