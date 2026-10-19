import { describe, expect, it } from "vitest";
import {
	fileStem,
	formatCatalogueFile,
	mergeCatalogueFiles,
	splitCatalogue
} from "../../../../scripts/lib/avatarCatalogue.mjs";
import fixture from "./fixtures/catalogue-v1.json";

type Entry = { path: string; json: unknown };

const entriesOf = (files: Map<string, unknown>): Entry[] =>
	[...files].map(([path, json]) => ({ path, json }));

/** A minimal valid tree: one shape, one palette, two eyes items. */
function smallTree(): Entry[] {
	return [
		{ path: "meta.json", json: { version: 1, featured: [] } },
		{
			path: "sockets/uni_round.json",
			json: {
				shape: "uni:round",
				eyes: [1, 2],
				mouth: [3, 4],
				head_top: [5, 6],
				body_center: [7, 8]
			}
		},
		{
			path: "palettes/basic.json",
			json: {
				id: "basic",
				order: 10,
				name: "Basic",
				price: 0,
				starter: true,
				swatches: [{ id: "basic:red", hex: "#ff0000" }]
			}
		},
		{
			path: "items/eyes/uni_eyes_a.json",
			json: { id: "uni:eyes_a", order: 20, slot: "eyes", name: "A" }
		},
		{
			path: "items/eyes/uni_eyes_b.json",
			json: { id: "uni:eyes_b", order: 10, slot: "eyes", name: "B" }
		}
	];
}

const replaceFile = (entries: Entry[], path: string, json: unknown): Entry[] =>
	entries.map((entry) => (entry.path === path ? { path, json } : entry));

describe("fileStem", () => {
	it("replaces every colon with an underscore", () => {
		expect(fileStem("uni:pastel:blush")).toBe("uni_pastel_blush");
		expect(fileStem("basic")).toBe("basic");
	});
});

describe("mergeCatalogueFiles", () => {
	it("sorts items by order inside a slot and removes the order field", () => {
		const doc = mergeCatalogueFiles(smallTree());
		expect(doc.items.map((item: { id: string }) => item.id)).toEqual(["uni:eyes_b", "uni:eyes_a"]);
		expect(doc.items[0]).not.toHaveProperty("order");
		expect(doc.palettes[0]).not.toHaveProperty("order");
		expect(doc.shape_sockets["uni:round"]).not.toHaveProperty("shape");
		expect(doc.shape_sockets["uni:round"].body_center).toEqual([7, 8]);
	});

	it("does not depend on the order of the input entries", () => {
		const forward = mergeCatalogueFiles(smallTree());
		const backward = mergeCatalogueFiles(smallTree().reverse());
		expect(backward).toEqual(forward);
	});

	it("puts items of an earlier slot first, whatever their order value", () => {
		const entries = [
			...smallTree(),
			{
				path: "items/shape/uni_round.json",
				json: { id: "uni:round", order: 999, slot: "shape", name: "Round" }
			}
		];
		const ids = mergeCatalogueFiles(entries).items.map((item: { id: string }) => item.id);
		expect(ids[0]).toBe("uni:round");
	});

	it("rejects an id repeated in two slot folders", () => {
		const entries = [
			...smallTree(),
			{
				path: "items/mouth/uni_eyes_a.json",
				json: { id: "uni:eyes_a", order: 10, slot: "mouth", name: "Dup" }
			}
		];
		expect(() => mergeCatalogueFiles(entries)).toThrow(
			/duplicate item id 'uni:eyes_a' in 'items\/eyes\/uni_eyes_a\.json' and 'items\/mouth\/uni_eyes_a\.json'/
		);
	});

	it("rejects two items with the same order in one slot", () => {
		const entries = replaceFile(smallTree(), "items/eyes/uni_eyes_b.json", {
			id: "uni:eyes_b",
			order: 20,
			slot: "eyes",
			name: "B"
		});
		expect(() => mergeCatalogueFiles(entries)).toThrow(/duplicate order 20 in slot eyes/);
	});

	it("rejects an item whose folder differs from its slot", () => {
		const entries = replaceFile(smallTree(), "items/eyes/uni_eyes_a.json", {
			id: "uni:eyes_a",
			order: 20,
			slot: "mouth",
			name: "A"
		});
		expect(() => mergeCatalogueFiles(entries)).toThrow(/folder 'eyes'/);
	});

	it("rejects a file name that does not match the id", () => {
		const entries = replaceFile(smallTree(), "items/eyes/uni_eyes_a.json", {
			id: "uni:eyes_z",
			order: 20,
			slot: "eyes",
			name: "A"
		});
		expect(() => mergeCatalogueFiles(entries)).toThrow(/stem 'uni_eyes_a'/);
	});

	it("rejects a missing or non-integer order", () => {
		const entries = replaceFile(smallTree(), "items/eyes/uni_eyes_a.json", {
			id: "uni:eyes_a",
			slot: "eyes",
			name: "A"
		});
		expect(() => mergeCatalogueFiles(entries)).toThrow(/integer 'order'/);
	});

	it("rejects an unknown slot folder", () => {
		const entries = [
			...smallTree(),
			{
				path: "items/cape/uni_cape.json",
				json: { id: "uni:cape", order: 10, slot: "cape", name: "Cape" }
			}
		];
		expect(() => mergeCatalogueFiles(entries)).toThrow(/unknown slot 'cape'/);
	});

	it("rejects a missing meta.json", () => {
		const entries = smallTree().filter((entry) => entry.path !== "meta.json");
		expect(() => mergeCatalogueFiles(entries)).toThrow(/meta\.json/);
	});

	it("rejects a stray file", () => {
		const entries = [...smallTree(), { path: "notes/todo.json", json: {} }];
		expect(() => mergeCatalogueFiles(entries)).toThrow(/unexpected file 'notes\/todo\.json'/);
	});
});

describe("splitCatalogue", () => {
	it("merge(split(x)) equals x for the v1 catalogue", () => {
		const files = splitCatalogue(fixture);
		expect(mergeCatalogueFiles(entriesOf(files))).toEqual(fixture);
	});

	it("keeps the item order of the document, including the starter order", () => {
		const merged = mergeCatalogueFiles(entriesOf(splitCatalogue(fixture)));
		expect(merged.items.map((item: { id: string }) => item.id)).toEqual(
			fixture.items.map((item) => item.id)
		);
		expect(merged.items[0].id).toBe("uni:classic");
	});

	it("never writes a colon into a file name", () => {
		for (const path of splitCatalogue(fixture).keys()) expect(path).not.toContain(":");
	});
});

describe("formatCatalogueFile", () => {
	it("uses tabs, one-line number arrays and a final newline", () => {
		const text = formatCatalogueFile({ eyes: [36, 26], name: "A" });
		expect(text).toBe('{\n\t"eyes": [36, 26],\n\t"name": "A"\n}\n');
	});
});
