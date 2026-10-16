import { describe, it, expect } from "vitest";
import { AfflictionManager } from "$components/game/affliction/afflictionManager";
import { OverrideChannel } from "$components/game/affliction/overrideChannel";
import type { ActiveAffliction, AfflictionModifier } from "$components/game/affliction/compose";

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

const faceOverride = (
	presentationId: string,
	target: string,
	priority: number,
	activationSeq: number,
	payload: Record<string, unknown>
): ActiveAffliction =>
	aff({
		presentationId,
		target,
		priority,
		activationSeq,
		modifiers: [mod("face.override", payload)]
	});

describe("OverrideChannel: face override lookup", () => {
	it("returns undefined when nothing is active", () => {
		const channel = new OverrideChannel(new AfflictionManager());
		expect(channel.override("c1")).toBeUndefined();
	});

	it("returns texture and uvRect from the active face.override", () => {
		const mgr = new AfflictionManager();
		mgr.activate(
			faceOverride("fo", "card:c1", 1, 1, { texture: "bomb", uvRect: [0, 0, 0.5, 0.5] })
		);
		const channel = new OverrideChannel(mgr);
		expect(channel.override("c1")).toEqual({ texture: "bomb", uvRect: [0, 0, 0.5, 0.5] });
	});

	it("accepts a full card address as well as a bare id", () => {
		const mgr = new AfflictionManager();
		mgr.activate(faceOverride("fo", "card:c1", 1, 1, { texture: "bomb" }));
		const channel = new OverrideChannel(mgr);
		expect(channel.override("card:c1")).toEqual({ texture: "bomb" });
	});

	it("returns the highest-priority face.override on the card", () => {
		const mgr = new AfflictionManager();
		mgr.activate(faceOverride("lo", "card:c1", 1, 1, { texture: "low" }));
		mgr.activate(faceOverride("hi", "card:c1", 5, 2, { texture: "high" }));
		const channel = new OverrideChannel(mgr);
		expect(channel.override("c1")).toEqual({ texture: "high" });
	});

	it("does not leak an override from another card", () => {
		const mgr = new AfflictionManager();
		mgr.activate(faceOverride("fo", "card:c2", 1, 1, { texture: "bomb" }));
		const channel = new OverrideChannel(mgr);
		expect(channel.override("c1")).toBeUndefined();
	});

	it("returns undefined when an exclusive kind owns the target", () => {
		const mgr = new AfflictionManager();
		mgr.activate(
			aff({
				presentationId: "audio",
				target: "card:c1",
				priority: 9,
				activationSeq: 1,
				modifiers: [mod("audio.loop", { clip: "x" })]
			})
		);
		mgr.activate(faceOverride("fo", "card:c1", 1, 2, { texture: "bomb" }));
		const channel = new OverrideChannel(mgr);
		expect(channel.override("c1")).toBeUndefined();
	});

	it("ignores a malformed payload and empty overrides", () => {
		const mgr = new AfflictionManager();
		mgr.activate(
			aff({ presentationId: "empty", target: "card:c1", modifiers: [mod("face.override")] })
		);
		const channel = new OverrideChannel(mgr);
		expect(channel.override("c1")).toBeUndefined();

		const mgr2 = new AfflictionManager();
		mgr2.activate(
			faceOverride("bad", "card:c1", 1, 1, { uvRect: [0, 0, 1] as unknown as number[] })
		);
		expect(new OverrideChannel(mgr2).override("c1")).toBeUndefined();
	});

	it("tracks expiry", () => {
		const mgr = new AfflictionManager();
		mgr.activate(faceOverride("lo", "card:c1", 1, 1, { texture: "low" }));
		mgr.activate(faceOverride("hi", "card:c1", 5, 2, { texture: "high" }));
		const channel = new OverrideChannel(mgr);
		expect(channel.override("c1")).toEqual({ texture: "high" });
		mgr.expire("hi");
		expect(channel.override("c1")).toEqual({ texture: "low" });
		mgr.expire("lo");
		expect(channel.override("c1")).toBeUndefined();
	});
});
