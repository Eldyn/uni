import { describe, it, expect, vi } from "vitest";
import { EntityRuntime } from "$lib/components/game/entity/entityRuntime.svelte";
import type { Entity, EntityRenderer } from "$lib/components/game/entity/entityRenderer";

/** A renderer that records the lifecycle calls the runtime makes, so the
 *  renderer seam is asserted without mounting anything. */
function recordingRenderer(): EntityRenderer & {
	mounted: number[];
	updated: number[];
	unmounted: number[];
} {
	const mounted: number[] = [];
	const updated: number[] = [];
	const unmounted: number[] = [];
	return {
		mounted,
		updated,
		unmounted,
		mount(e: Entity) {
			mounted.push(e.instanceId);
		},
		update(e: Entity) {
			updated.push(e.instanceId);
		},
		unmount(id: number) {
			unmounted.push(id);
		}
	};
}

describe("EntityRuntime addressing", () => {
	it("spawns with a named handle and resolves it", () => {
		const rt = new EntityRuntime();
		rt.define("dice", { components: { "movement.static": {}, "lifetime.ttl": { seconds: 3 } } });
		const id = rt.spawn("dice", { as: "d1" });
		expect(rt.resolve("entity:d1")?.instanceId).toBe(id);
	});

	it("auto-assigns an id when no handle, and despawns on ttl", () => {
		const rt = new EntityRuntime();
		rt.define("dice", { components: { "movement.static": {}, "lifetime.ttl": { seconds: 1 } } });
		const id = rt.spawn("dice");
		rt.tick(0.5);
		expect(rt.liveCount).toBe(1);
		rt.tick(0.6);
		expect(rt.resolve(`entity:${id}`)).toBeUndefined();
		expect(rt.liveCount).toBe(0);
	});

	it("degrades on an over-cap spawn", () => {
		const warn = vi.spyOn(console, "warn").mockImplementation(() => {});
		const rt = new EntityRuntime({ maxEntities: 2 });
		rt.define("dice", { components: { "lifetime.untilDespawn": {} } });
		rt.spawn("dice");
		rt.spawn("dice");
		const dropped = rt.spawn("dice");
		expect(dropped).toBe(0);
		expect(rt.liveCount).toBe(2);
		expect(warn).toHaveBeenCalledWith(expect.stringContaining("cap"));
		warn.mockRestore();
	});

	it("auto-suffixes a duplicate handle and keeps the original resolvable", () => {
		const warn = vi.spyOn(console, "warn").mockImplementation(() => {});
		const rt = new EntityRuntime();
		rt.define("dice", { components: { "lifetime.untilDespawn": {} } });
		const first = rt.spawn("dice", { as: "d1" });
		const second = rt.spawn("dice", { as: "d1" });
		expect(second).not.toBe(first);
		expect(rt.resolve("entity:d1")?.instanceId).toBe(first);
		expect(rt.resolve("entity:d1#2")?.instanceId).toBe(second);
		expect(warn).toHaveBeenCalledWith(expect.stringContaining("d1"));
		warn.mockRestore();
	});

	it("drops a spawn of an unknown def with a warning, never throwing", () => {
		const warn = vi.spyOn(console, "warn").mockImplementation(() => {});
		const rt = new EntityRuntime();
		expect(rt.spawn("nope")).toBe(0);
		expect(rt.liveCount).toBe(0);
		expect(warn).toHaveBeenCalledWith(expect.stringContaining("nope"));
		warn.mockRestore();
	});

	it("tolerates an unknown component name", () => {
		const warn = vi.spyOn(console, "warn").mockImplementation(() => {});
		const rt = new EntityRuntime();
		rt.define("dice", { components: { "movement.bogus": {} } });
		const id = rt.spawn("dice");
		expect(id).toBeGreaterThan(0);
		expect(rt.liveCount).toBe(1);
		expect(warn).toHaveBeenCalledWith(expect.stringContaining("movement.bogus"));
		warn.mockRestore();
	});

	it("setState patches runtime state", () => {
		const rt = new EntityRuntime();
		rt.define("dice", { components: { "lifetime.untilDespawn": {} } });
		const id = rt.spawn("dice");
		rt.setState(id, { label: "seven" });
		expect(rt.resolve(`entity:${id}`)?.state.label).toBe("seven");
	});
});

describe("EntityRuntime systems", () => {
	function seedAnchors(
		rt: EntityRuntime,
		origin: [number, number, number],
		target: [number, number, number]
	) {
		rt.define("anchor", { components: { "movement.static": {} } });
		const o = rt.spawn("anchor");
		const t = rt.spawn("anchor");
		rt.setState(o, { position: origin });
		rt.setState(t, { position: target });
		rt.attachTo(o, "$origin");
		rt.attachTo(t, "$target");
	}

	it("arc interpolates two legs through the apex over its duration", () => {
		const rt = new EntityRuntime();
		seedAnchors(rt, [0, 0, 0], [0, 0, 10]);
		rt.define("proj", {
			components: { "movement.arc": { from: "$origin", to: "$target", height: 4 } }
		});
		const id = rt.spawn("proj");

		rt.tick(0.5);
		expect(rt.resolve(`entity:${id}`)?.state.position).toEqual([0, 4, 5]);

		rt.tick(0.25);
		expect(rt.resolve(`entity:${id}`)?.state.position).toEqual([0, 2, 7.5]);
	});

	it("degrades a missing anchor without throwing", () => {
		const warn = vi.spyOn(console, "warn").mockImplementation(() => {});
		const rt = new EntityRuntime();
		rt.define("proj", {
			components: { "movement.arc": { from: "$origin", to: "$target", height: 4 } }
		});
		const id = rt.spawn("proj");
		rt.tick(0.5);
		expect(rt.resolve(`entity:${id}`)?.state.position).toEqual([0, 0, 0]);
		expect(warn).toHaveBeenCalledWith(expect.stringContaining("$origin"));
		// Missing anchors warn once per entity/ref, not once per tick.
		rt.tick(0.5);
		expect(warn).toHaveBeenCalledTimes(2);
		warn.mockRestore();
	});

	it("oscillate keeps advancing without a lifetime component", () => {
		const rt = new EntityRuntime();
		rt.define("bob", {
			components: { "motion.oscillate": { axis: "y", amplitude: 1, hz: 1 } }
		});
		const id = rt.spawn("bob");
		rt.tick(0.25);
		expect(rt.resolve(`entity:${id}`)?.state.offset).toEqual([0, 1, 0]);
	});

	it("ballistic launches from $origin under speed/gravity", () => {
		const rt = new EntityRuntime();
		seedAnchors(rt, [0, 0, 0], [10, 0, 0]);
		rt.define("shot", {
			components: {
				"movement.ballistic": { from: "$origin", to: "$target", speed: 10, gravity: 0 }
			}
		});
		const id = rt.spawn("shot");

		rt.tick(0.5);
		expect(rt.resolve(`entity:${id}`)?.state.position).toEqual([5, 0, 0]);

		rt.tick(0.5);
		expect(rt.resolve(`entity:${id}`)?.state.position).toEqual([10, 0, 0]);
	});

	it("spin accumulates turns*2π*hz*dt", () => {
		const rt = new EntityRuntime();
		rt.define("top", {
			components: { "motion.spin": { axis: "y", turns: 2, hz: 1 } }
		});
		const id = rt.spawn("top");
		rt.tick(0.5);
		expect(rt.resolve(`entity:${id}`)?.state.angle).toBeCloseTo(2 * Math.PI);
	});

	it("untilDespawn never expires while ttl does", () => {
		const rt = new EntityRuntime();
		rt.define("perm", { components: { "lifetime.untilDespawn": {} } });
		rt.define("temp", { components: { "lifetime.ttl": { seconds: 1 } } });
		const perm = rt.spawn("perm");
		const temp = rt.spawn("temp");
		rt.tick(5);
		expect(rt.resolve(`entity:${perm}`)).toBeDefined();
		expect(rt.resolve(`entity:${temp}`)).toBeUndefined();
	});
});

describe("EntityRuntime renderer seam", () => {
	it("drives mount/update/unmount", () => {
		const renderer = recordingRenderer();
		const rt = new EntityRuntime({ renderer });
		rt.define("dice", { components: { "lifetime.untilDespawn": {} } });
		const id = rt.spawn("dice");
		rt.tick(0.1);
		rt.despawn(id);
		expect(renderer.mounted).toEqual([id]);
		expect(renderer.updated).toEqual([id]);
		expect(renderer.unmounted).toEqual([id]);
	});
});
