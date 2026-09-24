import { describe, it, expect } from "vitest";
import { createCardRegistry } from "$components/game/animation/cardRegistry.svelte";
import { EntityRuntime } from "$components/game/entity/entityRuntime.svelte";
import type { AnimationBeat } from "$components/game/animation/types";

/**
 * acceptance: the client-side dice chain, driven through the IR player.
 *
 * `mods/test/rules/dice_roll.json` is the server-side `roll_table` chain that
 * binds `$last_outcome` and emits `outcome_result` (tested in
 * `tests/unit/entities_presentations_test.cpp`); no backend IR emitter exists in
 * so this test drives the equivalent IR beat directly and asserts the
 * entity lifecycle and the `outcome.resolver` selection the chain describes.
 *
 * The `dice` components mirror `mods/test/entities/dice.json` (movement.arc and
 * audio.sound are dropped for a deterministic, anchor-free unit replay; the
 * resolver keys and ttl are verbatim).
 */
const DICE_COMPONENTS = {
	"visual.emoji": { glyph: "🎲", size: 0.4 },
	"movement.static": {},
	"motion.spin": { axis: "y", turns: 3, hz: 1.0 },
	"outcome.resolver": {
		"test:six": { animation: "land_6", sound: "clack" },
		"*": {}
	},
	"lifetime.ttl": { seconds: 3.0 }
};

const anchor = (): [number, number, number] => [0, 0, 0];

function spawnDiceBeat(key: string): AnimationBeat {
	return [
		{
			op: "spawnEntity",
			target: "entity:dice",
			payload: { entity: "dice", as: "dice", outcome: key }
		}
	];
}

async function playDice(registry: ReturnType<typeof createCardRegistry>, key: string) {
	await registry.enqueue([spawnDiceBeat(key)], anchor);
}

describe("dice acceptance: spawn → systems tick → ttl → despawn", () => {
	it("resolves the exact `test:six` descriptor for a forced outcome", async () => {
		const runtime = new EntityRuntime();
		const registry = createCardRegistry(runtime);
		runtime.define("dice", { components: DICE_COMPONENTS });

		await playDice(registry, "test:six");

		const dice = runtime.resolve("entity:dice");
		expect(dice).toBeDefined();
		expect(dice?.state.outcome).toEqual({ animation: "land_6", sound: "clack" });
		expect(dice?.state.outcomeKey).toBe("test:six");
		expect(runtime.liveCount).toBe(1);

		// The systems tick: motion.spin advances the entity's angle.
		runtime.tick(0.5);
		expect(runtime.resolve("entity:dice")?.state.angle).toBeGreaterThan(0);

		// lifetime.ttl expires at 3s and the runtime despawns it.
		runtime.tick(3.0);
		expect(runtime.liveCount).toBe(0);
		expect(runtime.resolve("entity:dice")).toBeUndefined();
	});

	it("falls back to the `*` descriptor for an uncovered key", async () => {
		const runtime = new EntityRuntime();
		const registry = createCardRegistry(runtime);
		runtime.define("dice", { components: DICE_COMPONENTS });

		await playDice(registry, "test:rare");

		expect(runtime.resolve("entity:dice")?.state.outcome).toEqual({});
	});

	it("records the key even when no descriptor resolves (tolerant miss)", async () => {
		const runtime = new EntityRuntime();
		const registry = createCardRegistry(runtime);
		runtime.define("dice", {
			components: { ...DICE_COMPONENTS, "outcome.resolver": { "test:six": {} } }
		});

		await playDice(registry, "test:uncovered");

		const dice = runtime.resolve("entity:dice");
		expect(dice).toBeDefined();
		expect(dice?.state.outcome).toBeUndefined();
		expect(dice?.state.outcomeKey).toBe("test:uncovered");
	});

	it("spawns without an outcome key when the IR step carries none", async () => {
		const runtime = new EntityRuntime();
		const registry = createCardRegistry(runtime);
		runtime.define("dice", { components: DICE_COMPONENTS });

		await registry.enqueue(
			[[{ op: "spawnEntity", target: "entity:dice", payload: { entity: "dice", as: "dice" } }]],
			anchor
		);

		expect(runtime.resolve("entity:dice")?.state.outcome).toBeUndefined();
		expect(runtime.liveCount).toBe(1);
	});
});
