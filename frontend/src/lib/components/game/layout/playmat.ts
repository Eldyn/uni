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

// How much of the frustum's width the felt spans in portrait. A "cover" fit
// there scales the 16:9 sheet by HEIGHT, which pushes the felt's left and
// right edges far outside the frustum (the table loses its shape) while the
// sheet's own transparent top margin — 298 of 1080 rows, 28% of the screen —
// renders as a dead band under the HUD. Fitting the felt to the width instead
// keeps the whole table on screen and shrinks that margin proportionally.
export const PORTRAIT_MAT_WIDTH_FILL = 0.94;

// How far past its drawn shape the felt may be stretched to fill a portrait
// screen's depth. The border is painted into the art, so a vertical stretch
// thickens the top and bottom edges against the sides; past roughly double it
// stops reading as the same table.
export const MAX_PORTRAIT_STRETCH = 2;

const ART_WIDTH = ART_RIGHT - ART_LEFT;
const ART_HEIGHT = ART_BOTTOM - ART_TOP;

/** The felt's natural depth-to-width ratio, i.e. the shape the art was drawn at. */
export const ART_ASPECT = ART_HEIGHT / ART_WIDTH;

// The felt's share of the sheet along each axis, as a fraction of the sheet's
// full size — what a desired felt box has to be divided by to get the sheet
// that carries it.
const FELT_WIDTH_SHARE = (RIGHT_FRACTION - LEFT_FRACTION) / 2;
const FELT_HEIGHT_SHARE = (BOTTOM_FRACTION - TOP_FRACTION) / 2;
// Where the felt's own center sits inside the sheet, as a fraction of the
// sheet's half-size. Non-zero on Z: the art is not centered on its sheet.
const FELT_CENTER_Z_SHARE = (TOP_FRACTION + BOTTOM_FRACTION) / 2;

/** A rectangle in sheet UV: u grows to the right, v grows up the screen. */
export interface UvRect {
	left: number;
	right: number;
	bottom: number;
	top: number;
}

/** A mat sheet's size and where its opaque felt sits on it. The loop and the
 *  ripple read this instead of assuming one sheet shape. */
export interface MatSheet {
	texelWidth: number;
	texelHeight: number;
	feltUvRect: UvRect;
}

function feltUvRectFromTexels(
	texelWidth: number,
	texelHeight: number,
	left: number,
	right: number,
	top: number,
	bottom: number
): UvRect {
	return {
		left: left / texelWidth,
		right: right / texelWidth,
		top: 1 - top / texelHeight,
		bottom: 1 - bottom / texelHeight
	};
}

/** playmat.png's opaque felt box: `magick playmat.png -alpha extract
 *  -threshold 10% -format %@ info:` -> 640x464+640+300. */
export const DESKTOP_MAT_SHEET: MatSheet = {
	texelWidth: SHEET_WIDTH,
	texelHeight: SHEET_HEIGHT,
	feltUvRect: feltUvRectFromTexels(SHEET_WIDTH, SHEET_HEIGHT, 640, 1280, 300, 764)
};

/** mobile_playmat.png's opaque felt box (the box ART_* above describes). */
export const PHONE_MAT_SHEET: MatSheet = {
	texelWidth: SHEET_WIDTH,
	texelHeight: SHEET_HEIGHT,
	feltUvRect: feltUvRectFromTexels(
		SHEET_WIDTH,
		SHEET_HEIGHT,
		ART_LEFT,
		ART_RIGHT,
		ART_TOP,
		ART_BOTTOM
	)
};

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

export interface MatPlacement {
	/** The SHEET's drawn size — what the mesh's plane geometry needs, in the
	 *  sheet's own (unrotated) width × height. */
	size: [number, number];
	/** World X/Z the sheet's own center goes to, so the felt inside it lands on
	 *  the box this placement was solved for. Zero for the landscape fit, where
	 *  the sheet is simply centered on the origin like everything else. */
	offsetX: number;
	offsetZ: number;
	/** The sheet is turned a quarter about the table's up axis, so its long
	 *  side runs away from the player — a landscape painting on a tall table.
	 *  A rotation, never a mirror: the cw arrows still read clockwise. */
	quarterTurn: boolean;
	/** Where the felt itself ends up. */
	bounds: MatBounds;
}

/** The felt oval's world-space edges for a sheet of `size` centered at `offsetZ`. */
function boundsOf(size: [number, number], offsetZ: number): MatBounds {
	const [width, height] = size;
	return {
		left: (width / 2) * LEFT_FRACTION,
		right: (width / 2) * RIGHT_FRACTION,
		far: offsetZ + (height / 2) * TOP_FRACTION,
		near: offsetZ + (height / 2) * BOTTOM_FRACTION
	};
}

/**
 * Landscape: "cover" the frustum, exactly as the CSS background it replaced.
 * The felt's natural 16:9-ish shape already matches a landscape window, so the
 * overflow is table the player can use rather than table that falls off-screen.
 * A small zoom on top of the cover fit enlarges the felt itself — the room the
 * two center piles now share — while the near edge is pinned so the local hand's
 * strip is untouched.
 */
export const LANDSCAPE_MAT_ZOOM = 1.12;

export function landscapeMatPlacement(halfWidth: number, halfHeight: number): MatPlacement {
	const base = coverSize(SHEET_WIDTH, SHEET_HEIGHT, halfWidth, halfHeight);
	const size: [number, number] = [base[0] * LANDSCAPE_MAT_ZOOM, base[1] * LANDSCAPE_MAT_ZOOM];
	// Anchor the felt's NEAR (player-side) edge exactly where the base fit put
	// it, so the zoom only pushes the far edge and the sides outward. Growing it
	// symmetrically would drag the near edge down over the strip the local hand
	// is fitted into and shrink the player's own cards — the opposite of the
	// extra hand room this change is for.
	const baseNear = boundsOf(base, 0).near;
	const offsetZ = baseNear - (size[1] / 2) * BOTTOM_FRACTION;
	return { size, offsetX: 0, offsetZ, quarterTurn: false, bounds: boundsOf(size, offsetZ) };
}

/**
 * Portrait: draw the felt to an explicit box instead of fitting the sheet.
 * Covering crops the table's own left/right edges off-screen, and fitting the
 * sheet's width leaves the art's natural landscape shape floating in the
 * middle of a tall screen with a dead band above and below it. A phone wants a
 * PORTRAIT table, so the felt is stretched to the box the caller has free —
 * bounded by MAX_PORTRAIT_STRETCH, past which the border art reads as visibly
 * thicker top and bottom than at the sides, and centered in the box when the
 * cap binds.
 *
 * @param feltWidth Full width the felt should span, in world units.
 * @param farZ World Z of the felt's far (top-of-screen) edge.
 * @param nearZ World Z of the felt's near edge.
 */
export function portraitMatPlacement(feltWidth: number, farZ: number, nearZ: number): MatPlacement {
	const naturalHeight = feltWidth * ART_ASPECT;
	const feltHeight = Math.min(nearZ - farZ, naturalHeight * MAX_PORTRAIT_STRETCH);
	const feltCenterZ = (farZ + nearZ) / 2;

	const size: [number, number] = [feltWidth / FELT_WIDTH_SHARE, feltHeight / FELT_HEIGHT_SHARE];
	const offsetZ = feltCenterZ - (size[1] / 2) * FELT_CENTER_Z_SHARE;
	return { size, offsetX: 0, offsetZ, quarterTurn: false, bounds: boundsOf(size, offsetZ) };
}

// Where the felt's own center sits across the sheet's width, as a fraction of
// the sheet's half-width — only needed once the sheet is turned and that axis
// becomes the table's depth.
const FELT_CENTER_X_SHARE = (LEFT_FRACTION + RIGHT_FRACTION) / 2;

/**
 * Portrait, turned: the felt box is taller than it is wide, and the art is
 * the other way round, so the sheet is laid down a quarter-turn and stretched
 * from there — about 1.5× on a phone instead of the 3× an upright sheet needs
 * to fill the same box. Stretch past MAX_PORTRAIT_STRETCH is refused and the
 * felt is centered in the box instead.
 *
 * Turned by +90° about Y, the sheet's right edge points away from the player
 * and its top edge points to the table's left.
 */
export function turnedPortraitMatPlacement(
	feltHalfWidth: number,
	feltHalfDepth: number,
	feltCenterZ: number
): MatPlacement {
	const naturalHalfDepth = feltHalfWidth / ART_ASPECT;
	const halfDepth = Math.min(feltHalfDepth, naturalHalfDepth * MAX_PORTRAIT_STRETCH);

	const size: [number, number] = [
		(2 * halfDepth) / FELT_WIDTH_SHARE,
		(2 * feltHalfWidth) / FELT_HEIGHT_SHARE
	];
	const offsetX = -(size[1] / 2) * FELT_CENTER_Z_SHARE;
	const offsetZ = feltCenterZ + (size[0] / 2) * FELT_CENTER_X_SHARE;
	return {
		size,
		offsetX,
		offsetZ,
		quarterTurn: true,
		bounds: {
			left: -feltHalfWidth,
			right: feltHalfWidth,
			far: feltCenterZ - halfDepth,
			near: feltCenterZ + halfDepth
		}
	};
}

/** The felt oval's world-space edges inside the given frustum, landscape fit. */
export function matBounds(halfWidth: number, halfHeight: number): MatBounds {
	return landscapeMatPlacement(halfWidth, halfHeight).bounds;
}
