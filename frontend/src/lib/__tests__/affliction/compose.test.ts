import { describe, it, expect } from "vitest";
import {
	compose,
	kindTarget,
	KindModes,
	type ActiveAffliction,
	type AfflictionModifier
} from "$components/game/affliction/compose";
import { AfflictionManager } from "$components/game/affliction/afflictionManager";

const mod = (kind: string, extra: Record<string, unknown> = {}): AfflictionModifier => ({
	kind,
	...extra
});

function aff(
	fields: Pick<ActiveAffliction, "presentationId" | "target" | "modifiers"> &
		Partial<ActiveAffliction>
): ActiveAffliction {
	return { priority: 0, activationSeq: 0, expiry: null, ...fields };
}

describe("compose: engine kind -> mode table", () => {
	it("matches the pinned table verbatim", () => {
		expect(KindModes).toEqual({
			"face.override": "override",
			"material.param": "override",
			"screen.effect": "additive",
			"particles.emitter": "additive",
			"audio.loop": "exclusive"
		});
	});
});

describe("compose: override keeps losers active, winner is max(priority, seq, id)", () => {
	it("picks the highest priority modifier for a kind on a target", () => {
		const lo = aff({
			presentationId: "lo",
			target: "card:c1",
			priority: 1,
			activationSeq: 1,
			modifiers: [mod("material.param", { value: 1 })]
		});
		const hi = aff({
			presentationId: "hi",
			target: "card:c1",
			priority: 5,
			activationSeq: 2,
			modifiers: [mod("material.param", { value: 2 })]
		});
		const out = compose([lo, hi], KindModes);
		expect(out.get(kindTarget("material.param", "card:c1"))).toEqual({
			kind: "material.param",
			value: 2
		});
	});

	it("breaks priority ties by activationSeq, then by presentationId", () => {
		const a = aff({
			presentationId: "a",
			target: "card:c1",
			priority: 3,
			activationSeq: 1,
			modifiers: [mod("material.param", { v: "a" })]
		});
		const b = aff({
			presentationId: "b",
			target: "card:c1",
			priority: 3,
			activationSeq: 2,
			modifiers: [mod("material.param", { v: "b" })]
		});
		expect(compose([a, b], KindModes).get(kindTarget("material.param", "card:c1"))).toEqual(
			mod("material.param", { v: "b" })
		);

		const sameSeqA = aff({
			presentationId: "aaa",
			target: "card:c2",
			priority: 3,
			activationSeq: 4,
			modifiers: [mod("material.param", { v: "aaa" })]
		});
		const sameSeqZ = aff({
			presentationId: "zzz",
			target: "card:c2",
			priority: 3,
			activationSeq: 4,
			modifiers: [mod("material.param", { v: "zzz" })]
		});
		expect(
			compose([sameSeqA, sameSeqZ], KindModes).get(kindTarget("material.param", "card:c2"))
		).toEqual(mod("material.param", { v: "zzz" }));
	});

	it("keeps override losers active in the manager", () => {
		const mgr = new AfflictionManager();
		mgr.activate(
			aff({
				presentationId: "lo",
				target: "card:c1",
				priority: 1,
				activationSeq: 1,
				modifiers: [mod("material.param", { value: 1 })]
			})
		);
		mgr.activate(
			aff({
				presentationId: "hi",
				target: "card:c1",
				priority: 5,
				activationSeq: 2,
				modifiers: [mod("material.param", { value: 2 })]
			})
		);
		expect(mgr.active).toHaveLength(2);
		expect(mgr.resolve(kindTarget("material.param", "card:c1"))).toEqual({
			kind: "material.param",
			value: 2
		});
	});
});

describe("compose: additive orders by priority and caps, dropping the lowest", () => {
	it("returns all active modifiers in priority order when under the cap", () => {
		const low = aff({
			presentationId: "low",
			target: "card:c1",
			priority: 1,
			activationSeq: 1,
			modifiers: [mod("screen.effect", { intensity: 1 })]
		});
		const mid = aff({
			presentationId: "mid",
			target: "card:c1",
			priority: 2,
			activationSeq: 2,
			modifiers: [mod("screen.effect", { intensity: 2 })]
		});
		const high = aff({
			presentationId: "high",
			target: "card:c1",
			priority: 3,
			activationSeq: 3,
			modifiers: [mod("screen.effect", { intensity: 3 })]
		});
		expect(
			compose([low, high, mid], KindModes).get(kindTarget("screen.effect", "card:c1"))
		).toEqual([
			mod("screen.effect", { intensity: 3 }),
			mod("screen.effect", { intensity: 2 }),
			mod("screen.effect", { intensity: 1 })
		]);
	});

	it("caps screen.effect by screenStackMax and drops the lowest", () => {
		const stack = [1, 2, 3, 4].map((n) =>
			aff({
				presentationId: `s${n}`,
				target: "card:c1",
				priority: n,
				activationSeq: n,
				modifiers: [mod("screen.effect", { n })]
			})
		);
		const out = compose(stack, KindModes, { maxAfflictions: 8, screenStackMax: 2 });
		expect(out.get(kindTarget("screen.effect", "card:c1"))).toEqual([
			mod("screen.effect", { n: 4 }),
			mod("screen.effect", { n: 3 })
		]);
	});

	it("caps particles.emitter by maxAfflictions and drops the lowest", () => {
		const emitters = [1, 2, 3].map((n) =>
			aff({
				presentationId: `e${n}`,
				target: "card:c1",
				priority: n,
				activationSeq: n,
				modifiers: [mod("particles.emitter", { n })]
			})
		);
		const out = compose(emitters, KindModes, { maxAfflictions: 2, screenStackMax: 3 });
		expect(out.get(kindTarget("particles.emitter", "card:c1"))).toEqual([
			mod("particles.emitter", { n: 3 }),
			mod("particles.emitter", { n: 2 })
		]);
	});
});

describe("compose: exclusive is target-level single-owner", () => {
	it("holds only the max-priority exclusive affliction on a target", () => {
		const lo = aff({
			presentationId: "lo",
			target: "card:c1",
			priority: 1,
			activationSeq: 1,
			modifiers: [mod("audio.loop", { clip: "lo" })]
		});
		const hi = aff({
			presentationId: "hi",
			target: "card:c1",
			priority: 5,
			activationSeq: 2,
			modifiers: [mod("audio.loop", { clip: "hi" })]
		});
		const out = compose([lo, hi], KindModes);
		expect(out.get(kindTarget("audio.loop", "card:c1"))).toEqual(mod("audio.loop", { clip: "hi" }));
		expect(out.size).toBe(1);
	});

	it("makes an exclusive target single-owner across all kinds", () => {
		const audio = aff({
			presentationId: "audio",
			target: "card:c1",
			priority: 5,
			activationSeq: 1,
			modifiers: [mod("audio.loop", { clip: "x" })]
		});
		const face = aff({
			presentationId: "face",
			target: "card:c1",
			priority: 9,
			activationSeq: 2,
			modifiers: [mod("face.override", { texture: "bomb" })]
		});
		const out = compose([audio, face], KindModes);
		expect(out.size).toBe(1);
		expect(out.get(kindTarget("face.override", "card:c1"))).toEqual(
			mod("face.override", { texture: "bomb" })
		);
		expect(out.get(kindTarget("audio.loop", "card:c1"))).toBeUndefined();
	});
});

describe("compose: tolerant of unknown input", () => {
	it("degrades an unknown kind without throwing", () => {
		const weird = aff({
			presentationId: "weird",
			target: "card:c1",
			priority: 1,
			activationSeq: 1,
			modifiers: [mod("mod.unknown", { x: 1 })]
		});
		const out = compose([weird], KindModes);
		expect(out.get(kindTarget("mod.unknown", "card:c1"))).toEqual(mod("mod.unknown", { x: 1 }));
	});

	it("returns an empty map for an empty active set", () => {
		expect(compose([], KindModes).size).toBe(0);
	});

	it("composes the same kind independently per target", () => {
		const c1 = aff({
			presentationId: "c1",
			target: "card:c1",
			priority: 1,
			activationSeq: 1,
			modifiers: [mod("material.param", { on: "c1" })]
		});
		const c2 = aff({
			presentationId: "c2",
			target: "card:c2",
			priority: 2,
			activationSeq: 2,
			modifiers: [mod("material.param", { on: "c2" })]
		});
		const out = compose([c1, c2], KindModes);
		expect(out.get(kindTarget("material.param", "card:c1"))).toEqual(
			mod("material.param", { on: "c1" })
		);
		expect(out.get(kindTarget("material.param", "card:c2"))).toEqual(
			mod("material.param", { on: "c2" })
		);
	});
});

describe("AfflictionManager: activation, expiry and resolution", () => {
	it("rejects exclusive losers at activation rather than suppressing them", () => {
		const mgr = new AfflictionManager();
		mgr.activate(
			aff({
				presentationId: "lo",
				target: "card:c1",
				priority: 1,
				activationSeq: 1,
				modifiers: [mod("audio.loop", { clip: "lo" })]
			})
		);
		mgr.activate(
			aff({
				presentationId: "hi",
				target: "card:c1",
				priority: 5,
				activationSeq: 2,
				modifiers: [mod("audio.loop", { clip: "hi" })]
			})
		);
		expect(mgr.active).toHaveLength(1);
		expect(mgr.active[0].presentationId).toBe("hi");
		expect(mgr.resolve(kindTarget("audio.loop", "card:c1"))).toEqual(
			mod("audio.loop", { clip: "hi" })
		);
	});

	it("rejects a lower-priority non-exclusive on an exclusive target", () => {
		const mgr = new AfflictionManager();
		mgr.activate(
			aff({
				presentationId: "audio",
				target: "card:c1",
				priority: 5,
				activationSeq: 1,
				modifiers: [mod("audio.loop", { clip: "x" })]
			})
		);
		mgr.activate(
			aff({
				presentationId: "face",
				target: "card:c1",
				priority: 1,
				activationSeq: 2,
				modifiers: [mod("face.override", { texture: "bomb" })]
			})
		);
		expect(mgr.active).toHaveLength(1);
		expect(mgr.active[0].presentationId).toBe("audio");
		expect(mgr.resolve(kindTarget("face.override", "card:c1"))).toBeUndefined();
	});

	it("recomputes on expiry and reinstates the next override winner", () => {
		const mgr = new AfflictionManager();
		mgr.activate(
			aff({
				presentationId: "lo",
				target: "card:c1",
				priority: 1,
				activationSeq: 1,
				modifiers: [mod("material.param", { value: 1 })]
			})
		);
		mgr.activate(
			aff({
				presentationId: "hi",
				target: "card:c1",
				priority: 5,
				activationSeq: 2,
				modifiers: [mod("material.param", { value: 2 })]
			})
		);
		expect(mgr.resolve(kindTarget("material.param", "card:c1"))).toEqual(
			mod("material.param", { value: 2 })
		);
		mgr.expire("hi");
		expect(mgr.active).toHaveLength(1);
		expect(mgr.resolve(kindTarget("material.param", "card:c1"))).toEqual(
			mod("material.param", { value: 1 })
		);
	});

	it("returns undefined for an unknown kind/target and never throws", () => {
		const mgr = new AfflictionManager();
		expect(mgr.resolve(kindTarget("nope.nope", "card:c9"))).toBeUndefined();
		expect(() => mgr.expire("never-activated")).not.toThrow();
	});
});
