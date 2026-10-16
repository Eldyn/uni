import { describe, it, expect, vi } from "vitest";
import { ModGlossary } from "$lib/glossary/modGlossary";

function makeRegistry() {
	const report = { warn: vi.fn(), error: vi.fn() };
	return { registry: new ModGlossary(report), report };
}

const appleEntry = {
	id: "my_mod:apple",
	label_key: "my_mod:apple.label",
	description_key: "my_mod:apple.description",
	source: "card"
};

describe("ModGlossary listing", () => {
	it("reads the entries of a mod listing", () => {
		const { registry } = makeRegistry();
		registry.ingestMods([{ id: "my_mod", glossary: [appleEntry] }]);
		expect(registry.get("my_mod:apple")).toEqual({
			id: "my_mod:apple",
			modId: "my_mod",
			labelKey: "my_mod:apple.label",
			descriptionKey: "my_mod:apple.description",
			source: "card"
		});
	});

	it("ignores an entry whose namespace is not the listing mod", () => {
		const { registry, report } = makeRegistry();
		registry.ingestMods([
			{ id: "other_mod", glossary: [{ ...appleEntry, id: "my_mod:apple" }] },
			{ id: "other_mod", glossary: [{ ...appleEntry, id: "uni:turn" }] }
		]);
		expect(registry.get("my_mod:apple")).toBeUndefined();
		expect(registry.get("uni:turn")).toBeUndefined();
		expect(report.error).toHaveBeenCalledTimes(2);
	});

	it("keeps the first of two entries with the same id", () => {
		const { registry, report } = makeRegistry();
		registry.ingestMods([
			{
				id: "my_mod",
				glossary: [appleEntry, { ...appleEntry, label_key: "my_mod:second.label" }]
			}
		]);
		expect(registry.get("my_mod:apple")?.labelKey).toBe("my_mod:apple.label");
		expect(report.error).toHaveBeenCalledTimes(1);
	});

	it("skips malformed items without a throw", () => {
		const { registry } = makeRegistry();
		expect(() =>
			registry.ingestMods([
				null,
				{ id: 4, glossary: [] },
				{ id: "my_mod", glossary: [null, 7, { id: "my_mod:x" }, { ...appleEntry, label_key: 3 }] },
				{ id: "BAD ID", glossary: [appleEntry] }
			])
		).not.toThrow();
		expect(registry.get("my_mod:x")).toBeUndefined();
		expect(registry.get("my_mod:apple")).toBeUndefined();
	});

	it("ignores a payload that is not a list", () => {
		const { registry } = makeRegistry();
		expect(() => registry.ingestMods("nope")).not.toThrow();
		expect(() => registry.ingestMods(undefined)).not.toThrow();
	});

	it("replaces the previous listing on a new ingest", () => {
		const { registry } = makeRegistry();
		registry.ingestMods([{ id: "my_mod", glossary: [appleEntry] }]);
		registry.ingestMods([{ id: "my_mod", glossary: [] }]);
		expect(registry.get("my_mod:apple")).toBeUndefined();
	});

	it("clear drops every entry", () => {
		const { registry } = makeRegistry();
		registry.ingestMods([{ id: "my_mod", glossary: [appleEntry] }]);
		registry.clear();
		expect(registry.get("my_mod:apple")).toBeUndefined();
	});
});

describe("ModGlossary keys", () => {
	function entryOf(registry: ModGlossary, overrides: Record<string, unknown> = {}) {
		registry.ingestMods([{ id: "my_mod", glossary: [{ ...appleEntry, ...overrides }] }]);
		return registry.get("my_mod:apple")!;
	}

	it("accepts a key of the own namespace", () => {
		const { registry, report } = makeRegistry();
		const entry = entryOf(registry);
		expect(registry.keyOrNull(entry, "label_key", entry.labelKey)).toBe("my_mod:apple.label");
		expect(report.error).not.toHaveBeenCalled();
	});

	it("rejects a text in a key field and logs once", () => {
		const { registry, report } = makeRegistry();
		const entry = entryOf(registry, { label_key: "Hello World" });
		expect(registry.keyOrNull(entry, "label_key", entry.labelKey)).toBeNull();
		expect(registry.keyOrNull(entry, "label_key", entry.labelKey)).toBeNull();
		expect(report.error).toHaveBeenCalledTimes(1);
	});

	it("rejects a key of another namespace", () => {
		const { registry, report } = makeRegistry();
		const entry = entryOf(registry, { label_key: "other_mod:apple.label" });
		expect(registry.keyOrNull(entry, "label_key", entry.labelKey)).toBeNull();
		expect(report.error).toHaveBeenCalledTimes(1);
	});

	it("rejects a key over the length limit", () => {
		const { registry } = makeRegistry();
		const longKey = `my_mod:${"a".repeat(90)}`;
		const entry = entryOf(registry, { label_key: longKey });
		expect(registry.keyOrNull(entry, "label_key", longKey)).toBeNull();
	});

	it("rejects an empty word and a capital letter", () => {
		const { registry } = makeRegistry();
		const entry = entryOf(registry);
		expect(registry.keyOrNull(entry, "label_key", "my_mod:a..b")).toBeNull();
		expect(registry.keyOrNull(entry, "description_key", "my_mod:Apple")).toBeNull();
		expect(registry.keyOrNull(entry, "description_key", "my_mod:")).toBeNull();
	});
});
