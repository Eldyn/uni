import { describe, it, expect, vi } from "vitest";
import { EntityRuntime } from "$components/game/entity/entityRuntime.svelte";
import {
	createDefaultFlightPose,
	type RenderContext
} from "$components/game/animation/renderContext";
import { spawnEntityRenderer } from "$components/game/animation/stepRenderers/spawnEntity";
import { despawnEntityRenderer } from "$components/game/animation/stepRenderers/despawnEntity";
import { setEntityStateRenderer } from "$components/game/animation/stepRenderers/setEntityState";
import { attachToRenderer } from "$components/game/animation/stepRenderers/attachTo";
import { emitParticlesRenderer } from "$components/game/animation/stepRenderers/emitParticles";
import { emitSoundRenderer } from "$components/game/animation/stepRenderers/emitSound";
import { storeAudio } from "$stores/audio.svelte";

function ctxWith(entityRuntime: EntityRuntime): RenderContext {
	return {
		getPose: () => createDefaultFlightPose(),
		resolveAnchor: () => [0, 0, 0],
		entityRuntime
	};
}

function runtimeWithDice(): EntityRuntime {
	const runtime = new EntityRuntime();
	runtime.define("dice", { components: {} });
	return runtime;
}

describe("spawnEntity renderer", () => {
	it("spawns through the runtime and returns a persistent, non-blocking handle", () => {
		const runtime = runtimeWithDice();
		const spawn = vi.spyOn(runtime, "spawn");

		const handle = spawnEntityRenderer(
			{ op: "spawnEntity", target: "screen", payload: { entity: "dice", as: "d1" } },
			ctxWith(runtime)
		);

		expect(spawn).toHaveBeenCalledWith("dice", { as: "d1" });
		expect(handle.persistent).toBe(true);
		expect(handle.blocking).toBeNull();
	});

	it("skip despawns a transient entity but leaves a committed-persistent one", () => {
		const runtime = runtimeWithDice();
		const despawn = vi.spyOn(runtime, "despawn");

		const transient = spawnEntityRenderer(
			{ op: "spawnEntity", target: "screen", payload: { entity: "dice", as: "a" } },
			ctxWith(runtime)
		);
		const committed = spawnEntityRenderer(
			{
				op: "spawnEntity",
				target: "screen",
				payload: { entity: "dice", as: "b", persistent: true }
			},
			ctxWith(runtime)
		);

		transient.skip();
		committed.skip();

		expect(despawn).toHaveBeenCalledTimes(1);
	});

	it("degrades without throwing when the payload entity is missing", () => {
		const warn = vi.spyOn(console, "warn").mockImplementation(() => {});
		const runtime = runtimeWithDice();
		const handle = spawnEntityRenderer(
			{ op: "spawnEntity", target: "screen", payload: {} },
			ctxWith(runtime)
		);
		expect(handle.blocking).toBeNull();
		expect(warn).toHaveBeenCalled();
		warn.mockRestore();
	});
});

describe("despawnEntity renderer", () => {
	it("resolves the target and despawns it", () => {
		const runtime = runtimeWithDice();
		const id = runtime.spawn("dice", { as: "d1" });
		const despawn = vi.spyOn(runtime, "despawn");

		const handle = despawnEntityRenderer(
			{ op: "despawnEntity", target: "entity:d1" },
			ctxWith(runtime)
		);

		expect(despawn).toHaveBeenCalledWith(id);
		expect(handle.blocking).toBeNull();
	});

	it("degrades with a warning on an unresolvable target", () => {
		const warn = vi.spyOn(console, "warn").mockImplementation(() => {});
		expect(() =>
			despawnEntityRenderer(
				{ op: "despawnEntity", target: "entity:missing" },
				ctxWith(runtimeWithDice())
			)
		).not.toThrow();
		expect(warn).toHaveBeenCalled();
		warn.mockRestore();
	});
});

describe("setEntityState renderer", () => {
	it("resolves the target and applies the patch", () => {
		const runtime = runtimeWithDice();
		const id = runtime.spawn("dice", { as: "d1" });
		const setState = vi.spyOn(runtime, "setState");

		setEntityStateRenderer(
			{ op: "setEntityState", target: "entity:d1", payload: { patch: { angle: 1.5 } } },
			ctxWith(runtime)
		);

		expect(setState).toHaveBeenCalledWith(id, { angle: 1.5 });
	});
});

describe("attachTo renderer", () => {
	it("resolves the target and registers the anchor", () => {
		const runtime = runtimeWithDice();
		const id = runtime.spawn("dice", { as: "d1" });
		const attachTo = vi.spyOn(runtime, "attachTo");

		attachToRenderer(
			{ op: "attachTo", target: "entity:d1", payload: { anchor: "discard-pile" } },
			ctxWith(runtime)
		);

		expect(attachTo).toHaveBeenCalledWith(id, "discard-pile");
	});
});

describe("emitSound renderer", () => {
	it("plays the catalog clip through the audio store", () => {
		const playSfx = vi.spyOn(storeAudio, "playSfx").mockImplementation(() => {});
		const runtime = runtimeWithDice();

		const handle = emitSoundRenderer(
			{
				op: "emitSound",
				target: "screen",
				payload: { sound: "ui_click", pitch: 1.2, volume: 0.4 }
			},
			ctxWith(runtime)
		);

		expect(playSfx).toHaveBeenCalledWith("ui_click", { pitch: 1.2, volume: 0.4 });
		expect(handle.blocking).toBeNull();
		playSfx.mockRestore();
	});

	it("degrades with a warning when no sound id is given", () => {
		const warn = vi.spyOn(console, "warn").mockImplementation(() => {});
		const playSfx = vi.spyOn(storeAudio, "playSfx").mockImplementation(() => {});
		expect(() =>
			emitSoundRenderer(
				{ op: "emitSound", target: "screen", payload: {} },
				ctxWith(runtimeWithDice())
			)
		).not.toThrow();
		expect(playSfx).not.toHaveBeenCalled();
		expect(warn).toHaveBeenCalled();
		warn.mockRestore();
		playSfx.mockRestore();
	});
});

describe("emitParticles renderer", () => {
	it("is a tolerated stub that resolves immediately", () => {
		const warn = vi.spyOn(console, "warn").mockImplementation(() => {});
		const timeline = emitParticlesRenderer(
			{ op: "emitParticles", target: "screen", payload: { emitter: "spark" } },
			ctxWith(runtimeWithDice())
		);
		expect(timeline.progress()).toBe(1);
		expect(warn).toHaveBeenCalled();
		warn.mockRestore();
	});
});
