/**
 * @file avatar-export.mjs
 * @brief Exports Aseprite sources to the shipped 480x96 avatar strip PNGs.
 *
 * Wraps `aseprite -b file.aseprite --sheet strip.png --sheet-type horizontal`
 * for every `.aseprite` under a source directory, then checks each output's
 * IHDR. The `.aseprite` sources stay in the repo; the PNG is the shipped art.
 *
 * Usage (from the repo root):
 *   node frontend/scripts/avatar-export.mjs <src-dir> <out-dir>
 *   node frontend/scripts/avatar-export.mjs --check-only <out-dir>
 *
 * `--check-only` skips Aseprite and only validates the PNGs already present.
 * Every file prints one line; any problem makes the process exit non-zero.
 */
import { spawnSync } from "node:child_process";
import { mkdirSync, readdirSync, readFileSync } from "node:fs";
import { basename, join } from "node:path";
import { pathToFileURL } from "node:url";

const FRAME_SIZE = 96;
const FRAME_COUNT = 5;
const STRIP_WIDTH = FRAME_SIZE * FRAME_COUNT;
const SOURCE_EXTENSION = ".aseprite";
const OUTPUT_EXTENSION = ".png";
const PNG_SIGNATURE = Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]);

/** The Aseprite batch arguments that write a horizontal sheet of every frame. */
export function buildAsepriteArgs(src, out) {
	return ["-b", src, "--sheet", out, "--sheet-type", "horizontal"];
}

/**
 * Problems with a baked strip buffer; empty when it is a valid 480x96 PNG.
 * The IHDR width and height sit big-endian at bytes 16 and 20.
 */
export function checkStrip(png) {
	if (png.length < 24 || !png.subarray(0, 8).equals(PNG_SIGNATURE)) {
		return ["not a PNG buffer"];
	}
	const width = png.readUInt32BE(16);
	const height = png.readUInt32BE(20);
	if (width !== STRIP_WIDTH || height !== FRAME_SIZE) {
		return [`expected ${STRIP_WIDTH}x${FRAME_SIZE}, got ${width}x${height}`];
	}
	return [];
}

/** File names with the given extension directly inside a directory, sorted. */
function listFiles(dir, extension) {
	return readdirSync(dir, { withFileTypes: true })
		.filter((entry) => entry.isFile() && entry.name.endsWith(extension))
		.map((entry) => join(dir, entry.name))
		.sort();
}

/** Validates one PNG, prints its single result line and reports a problem. */
function checkFile(file) {
	const problems = checkStrip(readFileSync(file));
	console.log(`${file}: ${problems.length > 0 ? problems.join("; ") : "ok"}`);
	return problems.length > 0;
}

/** Exports every source under `srcDir` into `outDir`, then checks each PNG. */
function exportDirectory(srcDir, outDir) {
	const sources = listFiles(srcDir, SOURCE_EXTENSION);
	if (sources.length === 0) {
		console.log(`no ${SOURCE_EXTENSION} files in ${srcDir}`);
		return 0;
	}
	mkdirSync(outDir, { recursive: true });
	let failed = 0;
	for (const source of sources) {
		const out = join(outDir, `${basename(source, SOURCE_EXTENSION)}${OUTPUT_EXTENSION}`);
		const run = spawnSync("aseprite", buildAsepriteArgs(source, out), { stdio: "inherit" });
		if (run.error) {
			console.log(`${source}: ${run.error.message}`);
			failed++;
			continue;
		}
		if (run.status !== 0) {
			console.log(`${source}: aseprite exited ${run.status}`);
			failed++;
			continue;
		}
		if (checkFile(out)) failed++;
	}
	return failed;
}

/** Only validates the PNGs already present in a directory. */
function checkDirectory(dir) {
	let failed = 0;
	for (const file of listFiles(dir, OUTPUT_EXTENSION)) {
		if (checkFile(file)) failed++;
	}
	return failed;
}

function main(argv) {
	if (argv[0] === "--check-only") {
		if (!argv[1]) {
			console.error("usage: avatar-export.mjs --check-only <out-dir>");
			return 2;
		}
		return checkDirectory(argv[1]) > 0 ? 1 : 0;
	}
	const [srcDir, outDir] = argv;
	if (!srcDir || !outDir) {
		console.error("usage: avatar-export.mjs <src-dir> <out-dir>");
		return 2;
	}
	return exportDirectory(srcDir, outDir) > 0 ? 1 : 0;
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
	process.exitCode = main(process.argv.slice(2));
}
