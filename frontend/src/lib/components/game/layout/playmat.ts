/**
 * @file playmat.ts
 * @brief Where the playmat's painted oval actually lands in world space.
 *
 * playmat.png is a 16:9 sheet that is mostly transparent — the felt oval only
 * occupies a box in the middle of it. Everything that has to line up WITH the
 * felt (the local hand's size, the seats' distance from the table) needs that
 * box, not the sheet's own edges, so the measurement lives here once instead of
 * being re-guessed as a magic number at each call site.
 *
 * The bounds below are the PNG's opaque bounding box, read straight off the
 * asset (`magick playmat.png -alpha extract -threshold 10% -format %@ info:`
 * -> 687x464+621+298). If the art is ever redrawn, re-run that and update them.
 */

const SHEET_WIDTH = 1920;
const SHEET_HEIGHT = 1080;

const ART_LEFT = 621;
const ART_RIGHT = 1308;
const ART_TOP = 298;
const ART_BOTTOM = 762;

// Each edge as a signed share of the sheet's own half-size, measured from the
// sheet's center — which is where Playmat3D parents the mesh, so these survive
// whatever size the "cover" fit ends up choosing.
const LEFT_FRACTION = (ART_LEFT - SHEET_WIDTH / 2) / (SHEET_WIDTH / 2);
const RIGHT_FRACTION = (ART_RIGHT - SHEET_WIDTH / 2) / (SHEET_WIDTH / 2);
const TOP_FRACTION = (ART_TOP - SHEET_HEIGHT / 2) / (SHEET_HEIGHT / 2);
const BOTTOM_FRACTION = (ART_BOTTOM - SHEET_HEIGHT / 2) / (SHEET_HEIGHT / 2);

export interface MatBounds {
	/** World X of the felt's left/right edge. */
	left: number;
	right: number;
	/** World Z of the felt's far/near edge (-Z is the far side, +Z the local one). */
	far: number;
	near: number;
}

/**
 * "cover" fit, exactly as the CSS background-size it replaces: scale by
 * whichever axis needs more, so the art always fills the frustum.
 */
export function coverSize(
	imageWidth: number,
	imageHeight: number,
	halfWidth: number,
	halfHeight: number
): [number, number] {
	const scale = Math.max((halfWidth * 2) / imageWidth, (halfHeight * 2) / imageHeight);
	return [imageWidth * scale, imageHeight * scale];
}

/** The felt oval's world-space edges inside the given frustum. */
export function matBounds(halfWidth: number, halfHeight: number): MatBounds {
	const [width, height] = coverSize(SHEET_WIDTH, SHEET_HEIGHT, halfWidth, halfHeight);
	return {
		left: (width / 2) * LEFT_FRACTION,
		right: (width / 2) * RIGHT_FRACTION,
		far: (height / 2) * TOP_FRACTION,
		near: (height / 2) * BOTTOM_FRACTION
	};
}
