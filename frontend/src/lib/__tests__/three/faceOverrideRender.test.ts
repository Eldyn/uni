import { describe, it, expect } from "vitest";
import {
	faceKeyForOverride,
	entryWithOverride,
	type CardFaceKey,
	type AtlasEntry
} from "$components/game/three/cardFaceAtlas";
import { AfflictionManager } from "$components/game/affliction/afflictionManager";
import { OverrideChannel } from "$components/game/affliction/overrideChannel";

const baseKey: CardFaceKey = { type: "red", value: "5", turned: false };
const baseEntry: AtlasEntry = { page: 0, u0: 0.1, v0: 0.2, u1: 0.3, v1: 0.4 };

describe("faceKeyForOverride: the face key the renderer resolves", () => {
	it("leaves the base key untouched when no override is active", () => {
		expect(faceKeyForOverride(baseKey, undefined)).toBe(baseKey);
	});

	it("swaps the value for the override's texture name", () => {
		const key = faceKeyForOverride(baseKey, { texture: "bomb" });
		expect(key).not.toBe(baseKey);
		expect(key).toEqual({ type: "red", value: "bomb", turned: false });
	});

	it("keeps wild color and art version through the swap", () => {
		const wild: CardFaceKey = {
			type: "white",
			value: "jolly",
			turned: false,
			wildColor: "green",
			artVersion: "2"
		};
		expect(faceKeyForOverride(wild, { texture: "bomb" })).toEqual({
			type: "white",
			value: "bomb",
			turned: false,
			wildColor: "green",
			artVersion: "2"
		});
	});

	it("does not swap the back face of a turned card", () => {
		const turned: CardFaceKey = { type: "wild", value: "0", turned: true };
		expect(faceKeyForOverride(turned, { texture: "bomb" })).toBe(turned);
	});
});

describe("entryWithOverride: the atlas UV rect", () => {
	it("leaves the base entry untouched without a uvRect", () => {
		expect(entryWithOverride(baseEntry, { texture: "bomb" })).toBe(baseEntry);
		expect(entryWithOverride(baseEntry, undefined)).toBe(baseEntry);
	});

	it("applies a [u0, v0, w, h] rect onto the base page", () => {
		const entry = entryWithOverride(baseEntry, { uvRect: [0.5, 0.25, 0.25, 0.5] });
		expect(entry.page).toBe(0);
		expect(entry.u0).toBe(0.5);
		expect(entry.v0).toBe(0.25);
		expect(entry.u1).toBeCloseTo(0.75);
		expect(entry.v1).toBeCloseTo(0.75);
	});
});

describe("OverrideChannel → renderer face key", () => {
	it("an active face affliction changes the face key the renderer resolves", () => {
		const mgr = new AfflictionManager();
		mgr.activate({
			presentationId: "fo",
			target: "card:c1",
			priority: 1,
			activationSeq: 0,
			expiry: null,
			modifiers: [{ kind: "face.override", texture: "bomb" }]
		});
		const channel = new OverrideChannel(mgr);

		const resolved = faceKeyForOverride(baseKey, channel.override("c1"));
		expect(resolved.value).toBe("bomb");
		expect(resolved).not.toEqual(baseKey);
	});

	it("resolves a `seeded-random-tile` source through the injected resolver", () => {
		const mgr = new AfflictionManager();
		mgr.activate({
			presentationId: "lsd",
			target: "card:c1",
			priority: 1,
			activationSeq: 0,
			expiry: null,
			modifiers: [{ kind: "face.override", source: "seeded-random-tile" }]
		});
		const seen: string[] = [];
		const channel = new OverrideChannel(mgr, undefined, (source) => {
			seen.push(source);
			return { texture: "7" };
		});

		expect(channel.override("c1")).toEqual({ texture: "7" });
		expect(seen).toEqual(["seeded-random-tile"]);
	});

	it("degrades an unknown source without throwing", () => {
		const mgr = new AfflictionManager();
		mgr.activate({
			presentationId: "x",
			target: "card:c1",
			priority: 1,
			activationSeq: 0,
			expiry: null,
			modifiers: [{ kind: "face.override", source: "no-such-source" }]
		});
		const channel = new OverrideChannel(mgr, undefined, () => undefined);
		expect(channel.override("c1")).toBeUndefined();
	});
});
