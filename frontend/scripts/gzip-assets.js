/**
 * @file gzip-assets.js
 * @brief Writes a ".gz" sidecar next to every compressible built asset.
 *
 * The server picks these up when a client sends `Accept-Encoding: gzip`
 * (see http::PrecompressedVariant), so compression happens once at build time
 * at maximum level instead of per request. The JS bundle carrying
 * three.js/Threlte is the reason this exists.
 *
 * Run standalone with `node scripts/gzip-assets.js [outDir]`; the Vite build
 * invokes it through the gzipAssetsPlugin in vite.config.js.
 */

import fs from "node:fs";
import path from "node:path";
import zlib from "node:zlib";

/**
 * Extensions worth compressing. Images and woff2 fonts are already compressed
 * formats; .wasm is here because the draco/basis decoders three.js pulls in are
 * some of the largest assets in the bundle and halve under gzip.
 */
const COMPRESSIBLE = new Set([
	".js",
	".css",
	".html",
	".svg",
	".json",
	".xml",
	".txt",
	".map",
	".wasm",
	".webmanifest"
]);

/** Below this, gzip framing overhead outweighs any saving. */
const MIN_BYTES = 1024;

/**
 * @brief Compresses one file if it is stale, and reports the byte counts.
 * @returns {{raw: number, gz: number} | null} Sizes, or null if skipped.
 */
function gzipFile(filePath) {
	const source = fs.statSync(filePath);
	const sidecarPath = `${filePath}.gz`;

	// Skip work the previous build already did: in `--watch` mode only the
	// chunks that actually changed need recompressing.
	const existing = fs.statSync(sidecarPath, { throwIfNoEntry: false });
	if (existing && existing.mtimeMs >= source.mtimeMs) {
		return { raw: source.size, gz: existing.size };
	}

	const compressed = zlib.gzipSync(fs.readFileSync(filePath), { level: 9 });

	// A sidecar bigger than the original would only ever cost bandwidth.
	if (compressed.length >= source.size) {
		fs.rmSync(sidecarPath, { force: true });
		return null;
	}

	fs.writeFileSync(sidecarPath, compressed);
	return { raw: source.size, gz: compressed.length };
}

/**
 * @brief Gzips every compressible file under `outDir`, pruning stale sidecars.
 * @param {string} outDir Directory holding the built output.
 * @returns {{files: number, raw: number, gz: number}} Aggregate byte counts.
 */
export function gzipAssets(outDir) {
	const totals = { files: 0, raw: 0, gz: 0 };
	if (!fs.existsSync(outDir)) return totals;

	const walk = (dir) => {
		if (!fs.existsSync(dir)) return;
		for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
			const full = path.join(dir, entry.name);

			if (entry.isDirectory()) {
				walk(full);
				continue;
			}

			// Orphaned sidecar: its source is gone (pruned chunk, renamed asset).
			if (entry.name.endsWith(".gz")) {
				if (!fs.existsSync(full.slice(0, -3))) fs.rmSync(full);
				continue;
			}

			const stat = fs.statSync(full, { throwIfNoEntry: false });
			if (!stat || stat.size < MIN_BYTES) continue;

			const sizes = gzipFile(full);
			if (!sizes) continue;

			totals.files += 1;
			totals.raw += sizes.raw;
			totals.gz += sizes.gz;
		}
	};

	walk(outDir);
	return totals;
}

const kib = (bytes) => `${(bytes / 1024).toFixed(1)} kB`;

/**
 * @brief Formats the one-line summary the build prints.
 */
export function formatGzipSummary({ files, raw, gz }) {
	if (files === 0) return "gzip: nothing to compress";
	const saved = raw === 0 ? 0 : (1 - gz / raw) * 100;
	return `gzip: ${files} file(s) ${kib(raw)} -> ${kib(gz)} (-${saved.toFixed(1)}%)`;
}

// Direct invocation: `node scripts/gzip-assets.js [outDir]`.
if (process.argv[1] && import.meta.url === new URL(process.argv[1], "file://").href) {
	const outDir = path.resolve(process.argv[2] ?? "../public");
	console.log(formatGzipSummary(gzipAssets(outDir)));
}
