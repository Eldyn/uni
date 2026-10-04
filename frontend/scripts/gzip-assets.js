/**
 * @file gzip-assets.js
 * @brief Writes ".br" and ".gz" sidecars next to every compressible built asset.
 *
 * The server picks these up when a client sends `Accept-Encoding: br` or
 * `gzip` (see http::SelectPrecompressed, Brotli preferred), so compression
 * happens once at build time at maximum level instead of per request. The JS bundle carrying
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
 * @brief Writes one sidecar if it is stale, or drops it when it doesn't pay off.
 * @returns {number | null} The sidecar's size, or null if none was kept.
 */
function writeSidecar(filePath, sidecarPath, compress) {
	const source = fs.statSync(filePath);

	// Skip work the previous build already did: in `--watch` mode only the
	// chunks that actually changed need recompressing.
	const existing = fs.statSync(sidecarPath, { throwIfNoEntry: false });
	if (existing && existing.mtimeMs >= source.mtimeMs) return existing.size;

	const compressed = compress(fs.readFileSync(filePath));

	// A sidecar bigger than the original would only ever cost bandwidth.
	if (compressed.length >= source.size) {
		fs.rmSync(sidecarPath, { force: true });
		return null;
	}

	fs.writeFileSync(sidecarPath, compressed);
	return compressed.length;
}

const gzipBuffer = (buffer) => zlib.gzipSync(buffer, { level: 9 });

const brotliBuffer = (buffer) =>
	zlib.brotliCompressSync(buffer, {
		params: {
			[zlib.constants.BROTLI_PARAM_QUALITY]: zlib.constants.BROTLI_MAX_QUALITY,
			[zlib.constants.BROTLI_PARAM_SIZE_HINT]: buffer.length
		}
	});

/**
 * @brief Compresses one file into its .gz and .br sidecars.
 * @returns {{raw: number, gz: number, br: number} | null} Sizes, or null if
 * neither sidecar was kept. A skipped sidecar reports the raw size.
 */
function compressFile(filePath) {
	const raw = fs.statSync(filePath).size;
	const gz = writeSidecar(filePath, `${filePath}.gz`, gzipBuffer);
	const br = writeSidecar(filePath, `${filePath}.br`, brotliBuffer);
	if (gz === null && br === null) return null;
	return { raw, gz: gz ?? raw, br: br ?? raw };
}

/**
 * @brief Compresses every compressible file under `outDir`, pruning stale sidecars.
 * @param {string} outDir Directory holding the built output.
 * @returns {{files: number, raw: number, gz: number, br: number}} Aggregate byte counts.
 */
export function gzipAssets(outDir) {
	const totals = { files: 0, raw: 0, gz: 0, br: 0 };
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
			if (entry.name.endsWith(".gz") || entry.name.endsWith(".br")) {
				if (!fs.existsSync(full.slice(0, -3))) fs.rmSync(full);
				continue;
			}

			const stat = fs.statSync(full, { throwIfNoEntry: false });
			if (!stat || stat.size < MIN_BYTES) continue;

			const sizes = compressFile(full);
			if (!sizes) continue;

			totals.files += 1;
			totals.raw += sizes.raw;
			totals.gz += sizes.gz;
			totals.br += sizes.br;
		}
	};

	walk(outDir);
	return totals;
}

const kib = (bytes) => `${(bytes / 1024).toFixed(1)} kB`;

/**
 * @brief Formats the one-line summary the build prints.
 */
export function formatGzipSummary({ files, raw, gz, br }) {
	if (files === 0) return "compression: nothing to compress";
	const saved = (compressed) => (raw === 0 ? 0 : (1 - compressed / raw) * 100).toFixed(1);
	return `compression: ${files} file(s) ${kib(raw)} -> gzip ${kib(gz)} (-${saved(gz)}%), brotli ${kib(br)} (-${saved(br)}%)`;
}

// Direct invocation: `node scripts/gzip-assets.js [outDir]`.
if (process.argv[1] && import.meta.url === new URL(process.argv[1], "file://").href) {
	const outDir = path.resolve(process.argv[2] ?? "../public");
	console.log(formatGzipSummary(gzipAssets(outDir)));
}
