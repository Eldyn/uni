import { describe, it, expect, beforeEach, vi } from "vitest";
import { storeCardDefs } from "$lib/stores/cardDefs.svelte";

function face(kind: string, extra: Record<string, unknown> = {}): Record<string, unknown> {
	return { kind, art_version: 1, ...extra };
}

function defsPayload(
	digest: string,
	mods: { id: string; index: number }[],
	kinds: { index: number; string_id: string; face?: unknown; tags?: string[] }[]
) {
	return { defs_digest: digest, mods, kinds };
}

function matchStart(digest: string) {
	return {
		defs_digest: digest,
		mods: [],
		deck_id: "vanilla:classic",
		deck_name: "Classic",
		settings: {}
	};
}

describe("storeCardDefs kind table", () => {
	beforeEach(() => {
		storeCardDefs.reset();
	});

	it("populates both lookups from a defs frame", () => {
		const installed = storeCardDefs.ingestDefs(
			defsPayload(
				"d1",
				[{ id: "vanilla", index: 0 }],
				[
					{
						index: 0,
						string_id: "vanilla:red_0",
						face: face("text", { color: "red", label: "0" }),
						tags: ["colored", "numbered"]
					},
					{
						index: 1,
						string_id: "vanilla:red_1",
						face: face("text", { color: "red", label: "1" }),
						tags: ["colored"]
					}
				]
			)
		);

		expect(installed).toBe(true);
		expect(storeCardDefs.digest).toBe("d1");

		const byIndex = storeCardDefs.lookupByIndex(0, 0);
		expect(byIndex?.string_id).toBe("vanilla:red_0");
		expect(byIndex?.face).toEqual({ kind: "text", color: "red", label: "0", art_version: 1 });
		expect(byIndex?.tags).toEqual(["colored", "numbered"]);

		const byString = storeCardDefs.lookupByStringId("vanilla:red_1");
		expect(byString?.mod_index).toBe(0);
		expect(byString?.kind_index).toBe(1);
	});

	it("recovers mod_index from the string_id namespace for multiple mods", () => {
		storeCardDefs.ingestDefs(
			defsPayload(
				"d1",
				[
					{ id: "vanilla", index: 0 },
					{ id: "extra", index: 1 }
				],
				[
					{ index: 0, string_id: "vanilla:red_0", face: face("text") },
					{ index: 0, string_id: "extra:bomb", face: face("emoji", { label: "💣" }) }
				]
			)
		);

		expect(storeCardDefs.lookupByIndex(0, 0)?.string_id).toBe("vanilla:red_0");
		expect(storeCardDefs.lookupByIndex(1, 0)?.string_id).toBe("extra:bomb");
	});

	it("resolves missing kinds to undefined without throwing", () => {
		storeCardDefs.ingestDefs(
			defsPayload(
				"d1",
				[{ id: "vanilla", index: 0 }],
				[{ index: 0, string_id: "vanilla:red_0", face: face("text") }]
			)
		);

		expect(storeCardDefs.lookupByIndex(7, 99)).toBeUndefined();
		expect(storeCardDefs.lookupByStringId("vanilla:nope")).toBeUndefined();
	});

	it("accepts a match_start whose digest matches the defs table", () => {
		storeCardDefs.ingestDefs(
			defsPayload(
				"d1",
				[{ id: "vanilla", index: 0 }],
				[{ index: 0, string_id: "vanilla:red_0", face: face("text", { color: "red", label: "0" }) }]
			)
		);
		expect(storeCardDefs.accepted).toBe(false);

		expect(storeCardDefs.confirmMatchStart(matchStart("d1"))).toBe(true);
		expect(storeCardDefs.accepted).toBe(true);
		expect(storeCardDefs.lookupByStringId("vanilla:red_0")).toBeDefined();
	});

	it("drops the table on a mismatched digest and recovers for a later valid table", () => {
		storeCardDefs.ingestDefs(
			defsPayload(
				"d1",
				[{ id: "vanilla", index: 0 }],
				[{ index: 0, string_id: "vanilla:red_0", face: face("text") }]
			)
		);
		expect(storeCardDefs.confirmMatchStart(matchStart("d1"))).toBe(true);

		expect(storeCardDefs.confirmMatchStart(matchStart("other"))).toBe(false);
		expect(storeCardDefs.accepted).toBe(false);
		// The mismatched table must not resolve any face.
		expect(storeCardDefs.lookupByStringId("vanilla:red_0")).toBeUndefined();
		expect(storeCardDefs.digest).toBeNull();

		// A later defs frame carrying the awaited digest is accepted cleanly.
		expect(
			storeCardDefs.ingestDefs(
				defsPayload(
					"other",
					[{ id: "vanilla", index: 0 }],
					[{ index: 0, string_id: "vanilla:blue_1", face: face("text") }]
				)
			)
		).toBe(true);
		expect(storeCardDefs.accepted).toBe(true);
		expect(storeCardDefs.lookupByStringId("vanilla:blue_1")).toBeDefined();
	});

	it("ignores a defs frame whose digest does not match an active match_start", () => {
		expect(storeCardDefs.confirmMatchStart(matchStart("d1"))).toBe(false);

		expect(storeCardDefs.ingestDefs(defsPayload("stale", [{ id: "vanilla", index: 0 }], []))).toBe(
			false
		);
		expect(storeCardDefs.digest).toBeNull();

		expect(
			storeCardDefs.ingestDefs(
				defsPayload(
					"d1",
					[{ id: "vanilla", index: 0 }],
					[{ index: 0, string_id: "vanilla:red_0", face: face("text") }]
				)
			)
		).toBe(true);
		expect(storeCardDefs.accepted).toBe(true);
	});

	it("clears everything on reset", () => {
		storeCardDefs.ingestDefs(
			defsPayload(
				"d1",
				[{ id: "vanilla", index: 0 }],
				[{ index: 0, string_id: "vanilla:red_0", face: face("text") }]
			)
		);
		storeCardDefs.confirmMatchStart(matchStart("d1"));
		expect(storeCardDefs.accepted).toBe(true);

		storeCardDefs.reset();
		expect(storeCardDefs.digest).toBeNull();
		expect(storeCardDefs.accepted).toBe(false);
		expect(storeCardDefs.lookupByStringId("vanilla:red_0")).toBeUndefined();
	});

	it("rejects a later defs whose digest conflicts with the confirmed one, keeping the confirmed table", () => {
		storeCardDefs.ingestDefs(
			defsPayload(
				"confirmed",
				[{ id: "vanilla", index: 0 }],
				[{ index: 0, string_id: "vanilla:red_0", face: face("text") }]
			)
		);
		expect(storeCardDefs.confirmMatchStart(matchStart("confirmed"))).toBe(true);
		expect(storeCardDefs.accepted).toBe(true);

		const warn = vi.spyOn(console, "warn").mockImplementation(() => {});
		const installed = storeCardDefs.ingestDefs(
			defsPayload(
				"intruder",
				[{ id: "vanilla", index: 0 }],
				[{ index: 0, string_id: "vanilla:blue_1", face: face("text") }]
			)
		);
		const warned = warn.mock.calls.length;
		warn.mockRestore();

		expect(installed).toBe(false);
		expect(warned).toBeGreaterThan(0);
		// The confirmed table stays the lookup source; the conflicting one never
		// becomes readable.
		expect(storeCardDefs.digest).toBe("confirmed");
		expect(storeCardDefs.accepted).toBe(true);
		expect(storeCardDefs.lookupByStringId("vanilla:red_0")).toBeDefined();
		expect(storeCardDefs.lookupByStringId("vanilla:blue_1")).toBeUndefined();
		expect(storeCardDefs.lookupByIndex(0, 0)?.string_id).toBe("vanilla:red_0");
	});

	it("treats a re-ingested defs with the confirmed digest as idempotent without warning", () => {
		const payload = defsPayload(
			"confirmed",
			[{ id: "vanilla", index: 0 }],
			[{ index: 0, string_id: "vanilla:red_0", face: face("text", { color: "red", label: "0" }) }]
		);
		storeCardDefs.ingestDefs(payload);
		expect(storeCardDefs.confirmMatchStart(matchStart("confirmed"))).toBe(true);

		const warn = vi.spyOn(console, "warn").mockImplementation(() => {});
		const installed = storeCardDefs.ingestDefs(payload);
		const warned = warn.mock.calls.length;
		warn.mockRestore();

		expect(installed).toBe(true);
		expect(warned).toBe(0);
		expect(storeCardDefs.accepted).toBe(true);
		expect(storeCardDefs.digest).toBe("confirmed");
		expect(storeCardDefs.lookupByIndex(0, 0)?.string_id).toBe("vanilla:red_0");
		expect(storeCardDefs.lookupByStringId("vanilla:red_0")?.face.label).toBe("0");
	});
});
