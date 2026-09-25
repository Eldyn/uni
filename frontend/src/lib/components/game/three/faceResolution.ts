/**
 * @file faceResolution.ts
 * @brief Pure face -> render-plan resolution.
 *
 * Given a `defs` face and a tier ceiling, decide which layer source the art
 * plane uses. This is the documented fallback chain, evaluated richest-first:
 *
 *   image art[T..low] -> bundle emoji[T..low] -> face.kind emoji glyph
 *                     -> procedural colour/value -> blank
 *
 * No three.js import: it unit-tests without a canvas.
 */

import { assetOrigin, isAllowedAssetUrl } from "./assetTrust";

/** @brief Asset quality tiers, richest first. */
export type QualityTier = "high" | "medium" | "low";

/** @brief Layer composition mode. */
export type ArtMode = "inset" | "replace" | "overlay";

/** @brief Art fit inside the target rect. */
export type ArtFit = "contain" | "cover" | "stretch";

/** @brief One resolved variant of one face slot (from the defs packet). */
export interface FaceSlotVariant {
	tier: QualityTier;
	/** File variants. */
	url?: string;
	hash?: string;
	/** Glyph variants. */
	value?: string;
}

/** @brief The face as carried in a `defs` kind entry. */
export interface ResolvedFace {
	kind: string;
	color?: string;
	label?: string;
	art?: string;
	art_mode?: ArtMode;
	art_fit?: ArtFit;
	keep?: string[];
	art_version?: number;
	slots?: Record<string, FaceSlotVariant[]>;
}

/** @brief Which source the art plane draws, or null for no art layer. */
export type ArtSource =
	{ source: "asset"; url: string; hash: string } | { source: "emoji"; glyph: string } | null;

/** @brief The layer plan the renderer builds meshes from. */
export interface FacePlan {
	kind: string;
	art: ArtSource;
	/** The fallback actually taken (diagnostics + tests). */
	fallback: "asset" | "bundle-emoji" | "face-emoji" | "procedural" | "blank";
	art_mode: ArtMode;
	art_fit: ArtFit;
	/** Layers drawn over the art. */
	keep: string[];
	color?: string;
	label?: string;
	/** True when the vanilla procedural atlas should draw the face. */
	procedural: boolean;
}

const TIER_ORDER: readonly QualityTier[] = ["high", "medium", "low"];

/** @brief Tiers from the ceiling down to `low` (inclusive), richest first. */
export function tiersUpTo(ceiling: QualityTier): QualityTier[] {
	const start = TIER_ORDER.indexOf(ceiling);
	return TIER_ORDER.slice(start === -1 ? TIER_ORDER.length - 1 : start);
}

/** @brief Default layers kept per art mode. */
export function defaultKeep(mode: ArtMode): string[] {
	return mode === "replace" ? [] : ["value", "border"];
}

function pickFile(
	variants: readonly FaceSlotVariant[],
	ceiling: QualityTier,
	origin: string
): FaceSlotVariant | null {
	for (const tier of tiersUpTo(ceiling)) {
		const found = variants.find(
			(v) => v.tier === tier && v.url && v.hash && isAllowedAssetUrl(v.url, origin)
		);
		if (found) return found;
	}
	return null;
}

function pickGlyph(
	variants: readonly FaceSlotVariant[],
	ceiling: QualityTier
): FaceSlotVariant | null {
	for (const tier of tiersUpTo(ceiling)) {
		const found = variants.find((v) => v.tier === tier && typeof v.value === "string");
		if (found) return found;
	}
	return null;
}

/**
 * @brief Resolve a face to a render plan. Always terminates with a plan.
 * @param face The defs face (undefined when the kind has none).
 * @param ceiling The effective tier ceiling.
 */
export function resolveFace(
	face: ResolvedFace | undefined,
	ceiling: QualityTier,
	origin: string = assetOrigin()
): FacePlan {
	const mode: ArtMode = face?.art_mode ?? "inset";
	const fit: ArtFit = face?.art_fit ?? "contain";
	const keep = face?.keep ?? defaultKeep(mode);
	const base = {
		kind: face?.kind ?? "blank",
		art_mode: mode,
		art_fit: fit,
		keep,
		color: face?.color,
		label: face?.label
	};

	if (!face) {
		return { ...base, kind: "blank", art: null, fallback: "blank", procedural: false };
	}

	const slots = face.slots ?? {};

	if (face.kind === "image") {
		const art = pickFile(slots.art ?? [], ceiling, origin);
		if (art?.url && art.hash) {
			return {
				...base,
				art: { source: "asset", url: art.url, hash: art.hash },
				fallback: "asset",
				procedural: false
			};
		}
		const bundleGlyph = pickGlyph(slots.emoji ?? [], ceiling);
		if (bundleGlyph?.value) {
			return {
				...base,
				art: { source: "emoji", glyph: bundleGlyph.value },
				fallback: "bundle-emoji",
				procedural: false
			};
		}
		if (face.label) {
			return {
				...base,
				art: { source: "emoji", glyph: face.label },
				fallback: "face-emoji",
				procedural: false
			};
		}
		return { ...base, art: null, fallback: "procedural", procedural: true };
	}

	if (face.kind === "emoji") {
		const bundleGlyph = pickGlyph(slots.emoji ?? [], ceiling);
		if (bundleGlyph?.value) {
			return {
				...base,
				art: { source: "emoji", glyph: bundleGlyph.value },
				fallback: "bundle-emoji",
				procedural: false
			};
		}
		if (face.label) {
			return {
				...base,
				art: { source: "emoji", glyph: face.label },
				fallback: "face-emoji",
				procedural: false
			};
		}
		return { ...base, art: null, fallback: "blank", procedural: false };
	}

	if (face.kind === "text") {
		return { ...base, art: null, fallback: "procedural", procedural: true };
	}

	return { ...base, art: null, fallback: "blank", procedural: false };
}
