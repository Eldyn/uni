/**
 * @file avatarCatalogue.mjs
 * @brief Merges the per-element catalogue tree into one catalogue document.
 *
 * INFO: the C++ loader `Catalogue::MergeDirectory` (src/avatar/catalogue.cpp)
 *       implements the same rules. Change both together.
 */

import { readdirSync, readFileSync } from "node:fs";
import { join } from "node:path";

export const SLOT_ORDER = [
	"shape",
	"eyes",
	"mouth",
	"head_top",
	"mask",
	"bodywear",
	"set",
	"motif"
];
export const SOCKET_KEYS = ["eyes", "mouth", "head_top", "body_center"];

function fail(message) {
	throw new Error(`avatar catalogue: ${message}`);
}

/** The file name stem of an element id: every colon becomes an underscore. */
export function fileStem(id) {
	return id.replaceAll(":", "_");
}

function requireObject(entry) {
	if (typeof entry.json !== "object" || entry.json === null || Array.isArray(entry.json)) {
		fail(`'${entry.path}' must be a JSON object`);
	}
}

function requireStem(entry, id) {
	if (typeof id !== "string" || id.length === 0) fail(`'${entry.path}' needs a string id`);
	const stem = entry.path
		.split("/")
		.pop()
		.replace(/\.json$/, "");
	if (stem !== fileStem(id)) {
		fail(`'${entry.path}' has stem '${stem}' but id '${id}' needs '${fileStem(id)}'`);
	}
}

function requireOrder(entry) {
	if (!Number.isInteger(entry.json.order)) fail(`'${entry.path}' needs an integer 'order'`);
	return entry.json.order;
}

function claimId(seen, id, path, kind) {
	if (seen.has(id)) fail(`duplicate ${kind} id '${id}' in '${seen.get(id)}' and '${path}'`);
	seen.set(id, path);
}

function withoutKey(object, key) {
	const { [key]: _removed, ...rest } = object;
	return rest;
}

/**
 * @param {{ path: string, json: unknown }[]} entries Files relative to the tree root.
 * @returns The merged catalogue document.
 */
export function mergeCatalogueFiles(entries) {
	const sorted = [...entries].sort((a, b) => (a.path < b.path ? -1 : a.path > b.path ? 1 : 0));
	let meta;
	const sockets = {};
	const palettes = [];
	const items = [];
	const socketIds = new Map();
	const paletteIds = new Map();
	const itemIds = new Map();
	const paletteOrders = new Set();
	const itemOrders = new Set();

	for (const entry of sorted) {
		requireObject(entry);
		const parts = entry.path.split("/");
		if (entry.path === "meta.json") {
			meta = entry.json;
		} else if (parts[0] === "sockets" && parts.length === 2) {
			requireStem(entry, entry.json.shape);
			claimId(socketIds, entry.json.shape, entry.path, "shape socket");
			sockets[entry.json.shape] = withoutKey(entry.json, "shape");
		} else if (parts[0] === "palettes" && parts.length === 2) {
			requireStem(entry, entry.json.id);
			claimId(paletteIds, entry.json.id, entry.path, "palette");
			const order = requireOrder(entry);
			if (paletteOrders.has(order)) fail(`duplicate palette order ${order} ('${entry.path}')`);
			paletteOrders.add(order);
			palettes.push({ order, palette: withoutKey(entry.json, "order") });
		} else if (parts[0] === "items" && parts.length === 3) {
			const slot = parts[1];
			if (!SLOT_ORDER.includes(slot)) fail(`unknown slot '${slot}' ('${entry.path}')`);
			if (entry.json.slot !== slot) {
				fail(`'${entry.path}' is in folder '${slot}' but its slot is '${entry.json.slot}'`);
			}
			requireStem(entry, entry.json.id);
			claimId(itemIds, entry.json.id, entry.path, "item");
			const order = requireOrder(entry);
			const orderKey = `${slot}:${order}`;
			if (itemOrders.has(orderKey)) {
				fail(`duplicate order ${order} in slot ${slot} ('${entry.path}')`);
			}
			itemOrders.add(orderKey);
			items.push({ rank: SLOT_ORDER.indexOf(slot), order, item: withoutKey(entry.json, "order") });
		} else {
			fail(`unexpected file '${entry.path}'`);
		}
	}

	if (!meta) fail("missing 'meta.json'");
	for (const key of Object.keys(meta)) {
		if (key !== "version" && key !== "featured") fail(`meta.json: unknown key '${key}'`);
	}

	palettes.sort((a, b) => a.order - b.order);
	items.sort((a, b) => a.rank - b.rank || a.order - b.order);
	return {
		version: meta.version,
		shape_sockets: sockets,
		palettes: palettes.map((entry) => entry.palette),
		items: items.map((entry) => entry.item),
		featured: meta.featured
	};
}

/**
 * Split a catalogue document into tree files.
 * @returns {Map<string, object>} relative path to file content.
 */
export function splitCatalogue(doc) {
	const files = new Map();
	files.set("meta.json", { version: doc.version, featured: doc.featured });
	for (const [shape, table] of Object.entries(doc.shape_sockets)) {
		files.set(`sockets/${fileStem(shape)}.json`, { shape, ...table });
	}
	doc.palettes.forEach((palette, index) => {
		files.set(`palettes/${fileStem(palette.id)}.json`, {
			id: palette.id,
			order: (index + 1) * 10,
			...palette
		});
	});
	const slotCounts = {};
	for (const item of doc.items) {
		slotCounts[item.slot] = (slotCounts[item.slot] ?? 0) + 10;
		files.set(`items/${item.slot}/${fileStem(item.id)}.json`, {
			id: item.id,
			order: slotCounts[item.slot],
			...item
		});
	}
	return files;
}

/** Tabs, number arrays on one line, final newline (matches the old file). */
export function formatCatalogueFile(value) {
	const text = JSON.stringify(value, null, "\t");
	const collapsed = text.replace(
		/\[\s*([^[\]{}]*?)\s*\]/g,
		(_match, body) => `[${body.replace(/\s*\n\s*/g, " ")}]`
	);
	return `${collapsed}\n`;
}

function listJsonFiles(root, relative = "") {
	const found = [];
	for (const entry of readdirSync(join(root, relative), { withFileTypes: true })) {
		const next = relative ? `${relative}/${entry.name}` : entry.name;
		if (entry.isDirectory()) found.push(...listJsonFiles(root, next));
		else if (entry.name.endsWith(".json")) found.push(next);
	}
	return found;
}

/** Relative paths of every json file under the tree, sorted. */
export function listCatalogueFiles(directory) {
	return listJsonFiles(directory).sort();
}

/** Read and merge the tree at `directory`. */
export function readMergedCatalogue(directory) {
	const entries = listCatalogueFiles(directory).map((path) => ({
		path,
		json: JSON.parse(readFileSync(join(directory, path), "utf8"))
	}));
	return mergeCatalogueFiles(entries);
}
