/**
 * @file migrate-keyword-namespace.mjs
 * @brief One-shot migration: namespaces bare mechanic keyword refs in catalogs.
 *
 * Every `[k=<mechanic>]` reference must become `[k=vanilla:<mechanic>]` so
 * `hasGlossaryKeyword` resolves the rekeyed glossary registry. Card kinds are
 * not touched (there are none yet). Run standalone:
 * `node scripts/migrate-keyword-namespace.mjs`.
 */

import { readFileSync, writeFileSync, readdirSync } from "node:fs";
import { join } from "node:path";

const dir = new URL("../messages/", import.meta.url);
const KNOWN = new Set([
	"draw",
	"skip",
	"reverse",
	"wild",
	"turn",
	"color",
	"draw_pile",
	"discard_pile",
	"play",
	"seven_zero",
	"draw_stacking",
	"force_play",
	"jump_in",
	"progressive",
	"elimination"
]);

for (const file of readdirSync(dir).filter((f) => f.endsWith(".json"))) {
	const path = join(dir.pathname, file);
	const raw = readFileSync(path, "utf8");
	const migrated = raw.replace(/\[k=([a-z0-9_-]+)\]/gi, (full, id) =>
		id.includes(":") || !KNOWN.has(id) ? full : `[k=vanilla:${id}]`
	);
	if (migrated !== raw) writeFileSync(path, migrated);
}
