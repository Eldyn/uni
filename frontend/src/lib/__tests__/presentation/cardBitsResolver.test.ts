import { beforeEach, describe, expect, it } from "vitest";
import { resolveCardBits } from "$lib/presentation/cardBitsResolver";
import { storeCardDefs } from "$lib/stores/cardDefs.svelte";

const MOD_INDEX_SHIFT = 24;
const KIND_INDEX_SHIFT = 12;

const bitsOf = (modIndex: number, kindIndex: number) =>
	(modIndex << MOD_INDEX_SHIFT) | (kindIndex << KIND_INDEX_SHIFT);

describe("resolveCardBits", () => {
	beforeEach(() => {
		storeCardDefs.reset();
		storeCardDefs.ingestDefs({
			defs_digest: "d1",
			mods: [
				{ id: "uni", index: 0 },
				{ id: "my_mod", index: 3 }
			],
			kinds: [
				{
					index: 7,
					string_id: "my_mod:zap",
					face: { kind: "text", color: "red", label: "Z", art_version: 1 },
					tags: []
				},
				{
					index: 8,
					string_id: "my_mod:plain",
					face: { kind: "text", label: "P", art_version: 1 },
					tags: []
				}
			]
		});
	});

	it("decodes a mod kind unknown to the core", () => {
		expect(resolveCardBits(bitsOf(3, 7))).toEqual({ kind: "my_mod:zap", color: "red" });
	});

	it("falls back to white when the face has no color", () => {
		expect(resolveCardBits(bitsOf(3, 8))).toEqual({ kind: "my_mod:plain", color: "white" });
	});

	it.each([NaN, Infinity, -1, 1.5, 4294967296])("gives nothing for %s", (bits) => {
		expect(resolveCardBits(bits)).toBeUndefined();
	});

	it("gives nothing for an unknown card", () => {
		expect(resolveCardBits(bitsOf(3, 99))).toBeUndefined();
	});
});
