/**
 * @file avatar-placeholders.mjs
 * @brief Writes a placeholder PNG for every avatar item asset that is missing.
 *
 * Real art replaces these later with no code change: the catalogue names the
 * asset path, so dropping a hand-authored strip in the same place wins. Only
 * paths listed in `avatar/catalogue/` that do not exist yet are written.
 *
 * Run standalone from the repo root: `node frontend/scripts/avatar-placeholders.mjs`.
 * It uses only Node built-ins (node:zlib for the deflate stream, a local CRC32
 * for the PNG chunks). A strip is 5 frames of 96x96 (480x96) with a 1 px bob
 * per frame; a still is a single 96x96 frame. The palette is hard-edged: every
 * pixel is fully transparent or fully opaque.
 *
 * The tint mask is painted as `(shade, 0, 0)` for region 1, `(0, shade, 0)`
 * for region 2 and `(0, 0, shade)` for region 3, so the per-channel tint in the
 * renderer can pick it up. Non-tintable items (regions 0) are painted white.
 * Regions 4-6 live in an extra mask file; this script does not write one yet.
 */

import { existsSync, mkdirSync, writeFileSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import { deflateSync } from "node:zlib";
import { readMergedCatalogue } from "./lib/avatarCatalogue.mjs";

const FRAME_SIZE = 96;
const FRAME_COUNT = 5;
const STRIP_WIDTH = FRAME_SIZE * FRAME_COUNT;
/** 1 px of bob per frame, the same offsets the catalogue gives "still" items. */
const FRAME_BOB = [0, -1, -2, -1, 0];

/** Baked shade a tint channel carries; greys only darken under multiply. */
const SHADE = 255;
/**
 * Index 0 is black. Indexes 1-3 are regions 1-3 in the R, G, B channels of the
 * item art file. Indexes 4-6 are regions 4-6 in the R, G, B channels of the
 * extra mask file (`asset.masks[0]`).
 */
export const REGION_COLOURS = [
	[0, 0, 0],
	[SHADE, 0, 0],
	[0, SHADE, 0],
	[0, 0, SHADE],
	[SHADE, 0, 0],
	[0, SHADE, 0],
	[0, 0, SHADE]
];
const WHITE = [255, 255, 255];

const CRC_TABLE = new Uint32Array(256);
for (let n = 0; n < 256; n++) {
	let c = n;
	for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
	CRC_TABLE[n] = c >>> 0;
}

/** CRC32 of one PNG chunk (type + data). */
function crc32(bytes) {
	let c = 0xffffffff;
	for (let i = 0; i < bytes.length; i++) c = CRC_TABLE[(c ^ bytes[i]) & 0xff] ^ (c >>> 8);
	return (c ^ 0xffffffff) >>> 0;
}

/** One length-prefixed, CRC-suffixed PNG chunk. */
function chunk(type, data) {
	const body = Buffer.concat([Buffer.from(type, "ascii"), data]);
	const out = Buffer.alloc(12 + data.length);
	out.writeUInt32BE(data.length, 0);
	body.copy(out, 4);
	out.writeUInt32BE(crc32(body), 8 + data.length);
	return out;
}

/**
 * @brief Encode raw RGBA pixels as a PNG buffer (8-bit, colour type 6).
 * @param {number} width
 * @param {number} height
 * @param {Uint8Array} rgba width*height*4 bytes, row-major
 * @returns {Buffer}
 */
export function encodePng(width, height, rgba) {
	const stride = width * 4;
	const raw = Buffer.alloc((stride + 1) * height);
	for (let y = 0; y < height; y++) {
		const rowStart = y * (stride + 1);
		raw[rowStart] = 0; // filter: none
		for (let x = 0; x < stride; x++) raw[rowStart + 1 + x] = rgba[y * stride + x];
	}
	const ihdr = Buffer.alloc(13);
	ihdr.writeUInt32BE(width, 0);
	ihdr.writeUInt32BE(height, 4);
	ihdr[8] = 8; // bit depth
	ihdr[9] = 6; // colour type: truecolour + alpha
	ihdr[10] = 0; // compression: deflate
	ihdr[11] = 0; // filter method: adaptive
	ihdr[12] = 0; // interlace: none
	return Buffer.concat([
		Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
		chunk("IHDR", ihdr),
		chunk("IDAT", deflateSync(raw, { level: 9 })),
		chunk("IEND", Buffer.alloc(0))
	]);
}

/** A fully transparent 96x96 frame. */
function blankFrame() {
	return new Uint8Array(FRAME_SIZE * FRAME_SIZE * 4);
}

function setPixel(frame, x, y, colour) {
	if (x < 0 || y < 0 || x >= FRAME_SIZE || y >= FRAME_SIZE) return;
	const i = (y * FRAME_SIZE + x) * 4;
	frame[i] = colour[0];
	frame[i + 1] = colour[1];
	frame[i + 2] = colour[2];
	frame[i + 3] = 255;
}

function clearPixel(frame, x, y) {
	if (x < 0 || y < 0 || x >= FRAME_SIZE || y >= FRAME_SIZE) return;
	const i = (y * FRAME_SIZE + x) * 4;
	frame[i] = 0;
	frame[i + 1] = 0;
	frame[i + 2] = 0;
	frame[i + 3] = 0;
}

function fillRect(frame, x0, y0, w, h, colour) {
	for (let y = y0; y < y0 + h; y++) for (let x = x0; x < x0 + w; x++) setPixel(frame, x, y, colour);
}

function clearRect(frame, x0, y0, w, h) {
	for (let y = y0; y < y0 + h; y++) for (let x = x0; x < x0 + w; x++) clearPixel(frame, x, y);
}

function fillCircle(frame, cx, cy, r, colour) {
	for (let y = -r; y <= r; y++)
		for (let x = -r; x <= r; x++)
			if (x * x + y * y <= r * r) setPixel(frame, cx + x, cy + y, colour);
}

/** Drawers keyed by slot; each paints one frame at bob offset `dy`. */
const DRAWERS = {
	shape(frame, dy, item) {
		const radius = item.id === "uni:tall" ? 24 : 26;
		fillCircle(frame, 48, 24 + dy, 16, REGION_COLOURS[1]);
		fillRect(frame, 40, 36 + dy, 16, 10, REGION_COLOURS[1]);
		fillCircle(frame, 48, 62 + dy, radius, REGION_COLOURS[1]);
	},
	eyes(frame, dy, item) {
		const cy = 26 + dy;
		if (item.id === "uni:eyes_sleepy") {
			fillRect(frame, 30, cy, 9, 3, REGION_COLOURS[1]);
			fillRect(frame, 57, cy, 9, 3, REGION_COLOURS[1]);
			return;
		}
		const radius = item.id === "uni:eyes_wide" ? 8 : 5;
		fillCircle(frame, 34, cy, radius, REGION_COLOURS[1]);
		fillCircle(frame, 62, cy, radius, REGION_COLOURS[1]);
	},
	mouth(frame, dy, item) {
		const cy = 30 + dy;
		if (item.id === "uni:mouth_flat") {
			fillRect(frame, 41, cy, 14, 3, REGION_COLOURS[1]);
			return;
		}
		if (item.id === "uni:mouth_open") {
			fillCircle(frame, 48, cy + 2, 6, REGION_COLOURS[1]);
			return;
		}
		fillCircle(frame, 48, cy, 8, REGION_COLOURS[1]);
		clearRect(frame, 38, cy - 9, 20, 9); // carve the top -> smile
	},
	head_top(frame, dy, item) {
		if (item.id === "uni:hat_crown") {
			fillRect(frame, 32, 12 + dy, 32, 8, REGION_COLOURS[1]);
			fillRect(frame, 34, 4 + dy, 8, 10, REGION_COLOURS[1]);
			fillRect(frame, 44, 0 + dy, 8, 14, REGION_COLOURS[1]);
			fillRect(frame, 54, 4 + dy, 8, 10, REGION_COLOURS[1]);
			return;
		}
		fillCircle(frame, 48, 14 + dy, 16, REGION_COLOURS[1]);
		fillRect(frame, 26, 16 + dy, 44, 5, REGION_COLOURS[1]); // brim
	},
	mask(frame, dy) {
		fillRect(frame, 26, 20 + dy, 44, 12, REGION_COLOURS[1]);
	},
	bodywear(frame, dy) {
		fillRect(frame, 30, 42 + dy, 36, 10, REGION_COLOURS[1]);
		fillRect(frame, 30, 50 + dy, 10, 16, REGION_COLOURS[1]); // hanging tail
	},
	set(frame, dy) {
		fillCircle(frame, 48, 26 + dy, 20, WHITE);
		fillRect(frame, 32, 44 + dy, 32, 10, WHITE);
		fillCircle(frame, 48, 68 + dy, 28, WHITE);
	},
	motif(frame, dy, item) {
		if (item.id === "uni:motif_patches") {
			fillRect(frame, 26, 46 + dy, 12, 12, REGION_COLOURS[1]);
			fillRect(frame, 42, 46 + dy, 12, 12, REGION_COLOURS[2]);
			fillRect(frame, 58, 46 + dy, 12, 12, REGION_COLOURS[3]);
			return;
		}
		fillRect(frame, 22, 44 + dy, 52, 5, REGION_COLOURS[1]);
		fillRect(frame, 22, 54 + dy, 52, 5, REGION_COLOURS[1]);
		fillRect(frame, 22, 64 + dy, 52, 5, REGION_COLOURS[1]);
	}
};

/** One 96x96 frame for an item at frame index 0..4. */
function frameFor(item, index) {
	const frame = blankFrame();
	DRAWERS[item.slot](frame, FRAME_BOB[index], item);
	return frame;
}

/** A 480x96 strip asset: five frames side by side. */
export function encodeStrip(item) {
	const rgba = new Uint8Array(STRIP_WIDTH * FRAME_SIZE * 4);
	const rowBytes = FRAME_SIZE * 4;
	for (let index = 0; index < FRAME_COUNT; index++) {
		const frame = frameFor(item, index);
		for (let y = 0; y < FRAME_SIZE; y++) {
			const src = y * rowBytes;
			const dst = (y * STRIP_WIDTH + index * FRAME_SIZE) * 4;
			rgba.set(frame.subarray(src, src + rowBytes), dst);
		}
	}
	return encodePng(STRIP_WIDTH, FRAME_SIZE, rgba);
}

/** A 96x96 still asset: one frame, the bob stored in the catalogue. */
export function encodeStill(item) {
	return encodePng(FRAME_SIZE, FRAME_SIZE, frameFor(item, 0));
}

function main() {
	const scriptDir = dirname(fileURLToPath(import.meta.url));
	const repoRoot = resolve(scriptDir, "../..");
	const assetRoot = join(repoRoot, "frontend/public/assets/avatar");
	const catalogue = readMergedCatalogue(join(repoRoot, "avatar/catalogue"));

	let written = 0;
	for (const item of catalogue.items) {
		const file = join(assetRoot, item.asset.path);
		if (existsSync(file)) continue;
		mkdirSync(dirname(file), { recursive: true });
		const png = item.asset.kind === "strip" ? encodeStrip(item) : encodeStill(item);
		writeFileSync(file, png);
		written++;
	}
	console.log(`avatar placeholders: wrote ${written} file(s) under ${assetRoot}`);
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) main();
