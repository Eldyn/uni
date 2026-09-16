/**
 * @file handGesture.ts
 * @brief Pure decision math for LocalHand3D.svelte's pointer gesture — no
 * `$state`, no `window` listeners, no DOM. The component still owns the
 * actual pointer/event wiring (it's inherently stateful and event-driven),
 * but the four decisions that wiring makes — has the pointer moved far enough
 * to count as a drag, how far should a pan-scroll move, which slot a dragged
 * card is now nearest to, and what the reordered id list looks like — are
 * plain functions of their inputs, so they're testable without mounting a
 * Threlte scene.
 */

export interface SlotX {
	x: number;
}

/** Whether a pointer that has moved `deltaPx` since the gesture started
 *  should be treated as a drag rather than a tap. */
export function pastDragThreshold(deltaPx: number, thresholdPx: number): boolean {
	return Math.abs(deltaPx) >= thresholdPx;
}

/** The hand's new scroll offset (in em) for a pan gesture that has moved
 *  `deltaPx` screen pixels since it started at `scrollStartEm`. Content
 *  follows the finger: dragging right reveals the cards off the left end,
 *  which is a decreasing scroll offset. */
export function computeScrollEm(
	scrollStartEm: number,
	deltaPx: number,
	worldPerPixelX: number,
	handEmToWorld: number
): number {
	return scrollStartEm - (deltaPx * worldPerPixelX) / handEmToWorld;
}

/** Index of the slot whose x-position is nearest `draggedX`. Assumes at
 *  least one slot; the caller only ever invokes this on a non-empty hand. */
export function findNearestSlotIndex(slots: SlotX[], draggedX: number): number {
	let targetIndex = 0;
	let bestDist = Infinity;
	slots.forEach((slot, i) => {
		const dist = Math.abs(slot.x - draggedX);
		if (dist < bestDist) {
			bestDist = dist;
			targetIndex = i;
		}
	});
	return targetIndex;
}

/**
 * Finds the slot index to exchange with during a drag.
 * Requires the dragged card to cover at least `threshold` (default 0.75, i.e. 75%)
 * of the distance to the neighbor slot before triggering an exchange.
 * This prevents exchanging too early and provides natural hysteresis so cards
 * do not flicker between slots.
 */
export function findReorderTargetIndex(
	slots: SlotX[],
	currentIndex: number,
	draggedX: number,
	threshold = 0.8
): number {
	if (slots.length <= 1 || currentIndex < 0 || currentIndex >= slots.length) {
		return Math.max(0, Math.min(slots.length - 1, currentIndex));
	}

	let targetIndex = currentIndex;

	// Check dragging right
	while (targetIndex < slots.length - 1) {
		const currX = slots[targetIndex].x;
		const nextX = slots[targetIndex + 1].x;
		const triggerX = currX + threshold * (nextX - currX);
		if (draggedX >= triggerX - 1e-5) {
			targetIndex++;
		} else {
			break;
		}
	}

	// Check dragging left
	while (targetIndex > 0) {
		const currX = slots[targetIndex].x;
		const prevX = slots[targetIndex - 1].x;
		const triggerX = currX - threshold * (currX - prevX);
		if (draggedX <= triggerX + 1e-5) {
			targetIndex--;
		} else {
			break;
		}
	}

	return targetIndex;
}

/** `orderIds` with the id at `fromIndex` moved to `toIndex`, everything else
 *  keeping its relative order. Returns the SAME array reference (no new copy)
 *  when the indices already match, so a caller can skip a reactive update. */
export function computeReorderedIds(
	orderIds: number[],
	fromIndex: number,
	toIndex: number
): number[] {
	if (fromIndex === toIndex) return orderIds;
	const next = [...orderIds];
	const [moved] = next.splice(fromIndex, 1);
	next.splice(toIndex, 0, moved);
	return next;
}
