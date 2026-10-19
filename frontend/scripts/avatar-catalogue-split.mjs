/**
 * @file avatar-catalogue-split.mjs
 * @brief One-off migration: splits avatar/catalogue.json into avatar/catalogue/.
 *
 * Run from the repo root: `node frontend/scripts/avatar-catalogue-split.mjs`.
 * It refuses to write when the target folder exists. After writing, it merges
 * the new tree and compares the result to the source document.
 */

import { existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import { isDeepStrictEqual } from "node:util";
import {
	formatCatalogueFile,
	readMergedCatalogue,
	splitCatalogue
} from "./lib/avatarCatalogue.mjs";

function main() {
	const repoRoot = resolve(dirname(fileURLToPath(import.meta.url)), "../..");
	const source = join(repoRoot, "avatar/catalogue.json");
	const target = join(repoRoot, "avatar/catalogue");
	if (existsSync(target)) {
		console.error(`avatar-catalogue-split: '${target}' already exists`);
		process.exit(1);
	}
	const doc = JSON.parse(readFileSync(source, "utf8"));
	for (const [path, content] of splitCatalogue(doc)) {
		const file = join(target, path);
		mkdirSync(dirname(file), { recursive: true });
		writeFileSync(file, formatCatalogueFile(content));
	}
	if (!isDeepStrictEqual(readMergedCatalogue(target), doc)) {
		console.error("avatar-catalogue-split: merged tree differs from the source");
		process.exit(1);
	}
	console.log(`avatar-catalogue-split: wrote ${target}; merge equals the source`);
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) main();
