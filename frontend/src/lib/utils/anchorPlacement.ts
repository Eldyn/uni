export type Side = "top" | "right" | "bottom" | "left";
export type Align = "start" | "center" | "end";

export interface Rect {
	left: number;
	top: number;
	width: number;
	height: number;
	right?: number;
	bottom?: number;
}

export interface Size {
	width: number;
	height: number;
}

export interface PlacementOptions {
	/** Preferred side to place the popover. Defaults to 'top'. */
	side?: Side;
	/** Alignment along the cross axis. Defaults to 'center'. */
	align?: Align;
	/** Gap in pixels between trigger rect and popover. Defaults to 8. */
	offset?: number;
	/** Minimum margin in pixels from viewport boundary. Defaults to 8. */
	margin?: number;
	/** Viewport dimensions. Defaults to window size if in browser, or 1024x768. */
	viewport?: Size;
	/** Optional existing rects to avoid overlapping (used by multi-tooltip engines). */
	avoidRects?: Rect[];
}

export interface PlacementResult {
	x: number;
	y: number;
	side: Side;
}

const OPPOSITE_SIDE: Record<Side, Side> = {
	top: "bottom",
	bottom: "top",
	left: "right",
	right: "left"
};

const ORTHOGONAL_SIDES: Record<Side, [Side, Side]> = {
	top: ["right", "left"],
	bottom: ["right", "left"],
	left: ["top", "bottom"],
	right: ["top", "bottom"]
};

function rectsOverlap(r1: Rect, r2: Rect): boolean {
	const r1Right = r1.right ?? r1.left + r1.width;
	const r1Bottom = r1.bottom ?? r1.top + r1.height;
	const r2Right = r2.right ?? r2.left + r2.width;
	const r2Bottom = r2.bottom ?? r2.top + r2.height;

	return !(r1.left >= r2Right || r1Right <= r2.left || r1.top >= r2Bottom || r1Bottom <= r2.top);
}

/**
 * Computes the optimal static side placement and clamped coordinates
 * for an anchored popover / tooltip relative to a trigger element.
 */
export function computeAnchorPlacement(
	triggerRect: Rect,
	contentSize: Size,
	options: PlacementOptions = {}
): PlacementResult {
	const preferredSide: Side = options.side ?? "top";
	const align: Align = options.align ?? "center";
	const offset: number = options.offset ?? 8;
	const margin: number = options.margin ?? 8;
	const viewport: Size = options.viewport ?? {
		width: typeof window !== "undefined" ? window.innerWidth : 1024,
		height: typeof window !== "undefined" ? window.innerHeight : 768
	};
	const avoidRects: Rect[] = options.avoidRects ?? [];

	const candidateSides: Side[] = [
		preferredSide,
		OPPOSITE_SIDE[preferredSide],
		...ORTHOGONAL_SIDES[preferredSide]
	];

	function getCoordinatesForSide(side: Side): { x: number; y: number } {
		let x = 0;
		let y = 0;

		if (side === "top") {
			y = triggerRect.top - contentSize.height - offset;
			if (align === "start") x = triggerRect.left;
			else if (align === "end") x = triggerRect.left + triggerRect.width - contentSize.width;
			else x = triggerRect.left + (triggerRect.width - contentSize.width) / 2;
		} else if (side === "bottom") {
			y = triggerRect.top + triggerRect.height + offset;
			if (align === "start") x = triggerRect.left;
			else if (align === "end") x = triggerRect.left + triggerRect.width - contentSize.width;
			else x = triggerRect.left + (triggerRect.width - contentSize.width) / 2;
		} else if (side === "left") {
			x = triggerRect.left - contentSize.width - offset;
			if (align === "start") y = triggerRect.top;
			else if (align === "end") y = triggerRect.top + triggerRect.height - contentSize.height;
			else y = triggerRect.top + (triggerRect.height - contentSize.height) / 2;
		} else if (side === "right") {
			x = triggerRect.left + triggerRect.width + offset;
			if (align === "start") y = triggerRect.top;
			else if (align === "end") y = triggerRect.top + triggerRect.height - contentSize.height;
			else y = triggerRect.top + (triggerRect.height - contentSize.height) / 2;
		}

		return { x, y };
	}

	function fitsMainAxis(side: Side, coords: { x: number; y: number }): boolean {
		if (side === "top") {
			return coords.y >= margin;
		}
		if (side === "bottom") {
			return coords.y + contentSize.height <= viewport.height - margin;
		}
		if (side === "left") {
			return coords.x >= margin;
		}
		if (side === "right") {
			return coords.x + contentSize.width <= viewport.width - margin;
		}
		return false;
	}

	let chosenSide: Side = preferredSide;
	let coords = getCoordinatesForSide(preferredSide);

	for (const side of candidateSides) {
		const candidateCoords = getCoordinatesForSide(side);
		if (fitsMainAxis(side, candidateCoords)) {
			// Also check collision with avoidRects if provided
			if (avoidRects.length > 0) {
				const popoverRect: Rect = {
					left: candidateCoords.x,
					top: candidateCoords.y,
					width: contentSize.width,
					height: contentSize.height
				};
				const overlaps = avoidRects.some((r) => rectsOverlap(popoverRect, r));
				if (!overlaps) {
					chosenSide = side;
					coords = candidateCoords;
					break;
				}
			} else {
				chosenSide = side;
				coords = candidateCoords;
				break;
			}
		}
	}

	// Clamp both axes so the popover remains strictly on-screen within margin boundaries
	const minX = margin;
	const maxX = Math.max(margin, viewport.width - margin - contentSize.width);
	const minY = margin;
	const maxY = Math.max(margin, viewport.height - margin - contentSize.height);

	const clampedX = Math.min(Math.max(coords.x, minX), maxX);
	const clampedY = Math.min(Math.max(coords.y, minY), maxY);

	return {
		x: Math.round(clampedX),
		y: Math.round(clampedY),
		side: chosenSide
	};
}
