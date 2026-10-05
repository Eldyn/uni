import { describe, expect, it } from "vitest";
import { getGlossaryEntry } from "$lib/glossary/glossary";

const messageFiles = import.meta.glob<string>("../../../../messages/*.json", {
	query: "?raw",
	import: "default",
	eager: true
});

describe("uni namespace in glossary keywords", () => {
	it("resolves a uni keyword to an entry that keeps the uni id", () => {
		expect(getGlossaryEntry("uni:draw")?.id).toBe("uni:draw");
	});

	it("does not resolve a vanilla keyword", () => {
		expect(getGlossaryEntry("vanilla:draw")).toBeNull();
	});

	it("holds no vanilla keyword in any message file", () => {
		const offenders = Object.entries(messageFiles)
			.filter(([, content]) => content.includes("[k=vanilla:"))
			.map(([path]) => path);
		expect(Object.keys(messageFiles).length).toBeGreaterThan(0);
		expect(offenders).toEqual([]);
	});
});

const nodeModule = (name: string) => import(/* @vite-ignore */ `node:${name}`);
const { readdirSync, readFileSync, statSync } = await nodeModule("fs");
const { dirname, join, relative, resolve } = await nodeModule("path");
const { fileURLToPath } = await nodeModule("url");

const repoRoot = resolve(dirname(fileURLToPath(import.meta.url)), "../../../../..");
const oldModName = "vanilla";
const scannedEntries = [
	"src",
	"include",
	"mods",
	"tests",
	"contract",
	"scripts",
	"frontend/src",
	"frontend/messages",
	"frontend/scripts",
	"README.md",
	"BUILD.md",
	"RELEASING.md",
	"ATTRIBUTION.md"
];
const scannedExtensions = /\.(cpp|hpp|ts|js|mjs|svelte|json|md|txt|py|sh|yml|css|glsl)$/;
const excludedNames = new Set([
	"CHANGELOG.md",
	"uni_rename_test.cpp",
	"uniNamespace.test.ts",
	"node_modules",
	"build",
	".svelte-kit",
	"docs",
	"assets",
	"package-lock.json"
]);

function collectFiles(path: string): string[] {
	if (excludedNames.has(path.split("/").pop() ?? "")) return [];
	let stats;
	try {
		stats = statSync(path);
	} catch {
		return [];
	}
	if (stats.isDirectory()) {
		return readdirSync(path).flatMap((name: string) => collectFiles(join(path, name)));
	}
	return scannedExtensions.test(path) ? [path] : [];
}

describe("uni rename contract", () => {
	it("keeps the old mod name out of every source, mod, test and message file", () => {
		const files = scannedEntries.flatMap((entry) => collectFiles(join(repoRoot, entry)));
		expect(files.length).toBeGreaterThan(100);
		const offenders = files
			.filter((file) => readFileSync(file, "utf8").toLowerCase().includes(oldModName))
			.map((file) => relative(repoRoot, file));
		expect(offenders).toEqual([]);
	});
});
