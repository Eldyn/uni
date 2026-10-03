<!-- Transparent DOM hit target laid over the owner's parked drawn card (the
     face-up card held at the draw pile between draw and the play/keep choice).
     Clicking it plays the card; dragging it and releasing over the discard pile
     plays it. Both route to the same play action the 3D hand calls
     (GameBoard's `play` → storeGame.playCard), so the held card is playable by
     pointer through the existing path rather than a second play mechanism.

     It is a DOM sibling of the canvas (not a WebGL mesh) because the parked
     card is drawn by AllCards3D from the card registry and has no hit geometry
     of its own; sitting outside the canvas also lets it be hit-tested and
     tested without a live scene. -->
<script lang="ts">
	let {
		cardId,
		leftPercent,
		topPercent,
		isOverDiscard,
		onPlay
	}: {
		/** Id of the parked drawn card this target plays. */
		cardId: number;
		/** Position of the parked card's centre, as viewport percentages. */
		leftPercent: number;
		topPercent: number;
		/** Whether a released pointer at `(clientX, clientY)` counts as a drop
		 *  on the discard pile. Absent means any release counts. */
		isOverDiscard?: (clientX: number, clientY: number) => boolean;
		onPlay: (cardId: number) => void;
	} = $props();

	// Below this a gesture is a click (play outright); above it, a drag that
	// must land on the discard pile. Mirrors LocalHand3D's own threshold.
	const DRAG_THRESHOLD_PX = 6;

	let dragging = $state(false);
	let moved = false;
	let startX = 0;
	let startY = 0;

	function onPointerDown(event: PointerEvent) {
		if (event.button !== 0) return;
		dragging = true;
		moved = false;
		startX = event.clientX;
		startY = event.clientY;
		// Keep receiving move/up after the pointer leaves the target while
		// dragging. jsdom lacks pointer capture, so it is optional.
		(event.currentTarget as HTMLElement).setPointerCapture?.(event.pointerId);
	}

	function onPointerMove(event: PointerEvent) {
		if (!dragging || moved) return;
		if (Math.hypot(event.clientX - startX, event.clientY - startY) < DRAG_THRESHOLD_PX) return;
		moved = true;
	}

	function onPointerUp(event: PointerEvent) {
		if (!dragging) return;
		dragging = false;
		const dropped = !moved || isOverDiscard?.(event.clientX, event.clientY) === true;
		if (dropped) onPlay(cardId);
	}
</script>

<div
	class="held-draw-hit"
	data-testid="held-draw-hit"
	role="button"
	aria-label="Play drawn card"
	style="left: {leftPercent}%; top: {topPercent}%"
	onpointerdown={onPointerDown}
	onpointermove={onPointerMove}
	onpointerup={onPointerUp}
></div>

<style>
	.held-draw-hit {
		position: absolute;
		/* A touch larger than the card so the whole face is an easy tap target. */
		width: calc(var(--cardSize, 5em) * 1.1);
		height: calc(var(--cardSize, 5em) * 1.5);
		transform: translate(-50%, -50%);
		z-index: 1;
		cursor: grab;
		touch-action: none;
	}
</style>
