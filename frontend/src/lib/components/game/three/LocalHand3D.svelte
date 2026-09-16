<!-- The local player's own hand: a straight, slightly overlapping row of
     cards (layout/handLine.ts), top-down like the rest of the board.

     How you play a card depends on whether the device can hover
     (layout/pointerMode.svelte.ts), not on how big its screen is:

       mouse — point at a card to preview it lifted, click to play it. One step,
               because the hover already showed you what you were committing to.
       touch — tap the card to pick it, then tap the discard pile to confirm.
               There is no preview to point with, and a mis-play is
               unrecoverable. Scene3D owns the selection so the pile can light
               up as the confirm target.

     A hand too wide for the screen stops compressing and becomes a scrollable
     strip: cards fade out at the ends rather than being cut off mid-card. Touch
     pans it by dragging any unpicked card (dragging the PICKED one reorders
     instead, and it's already lifted and outlined, so which mode you're in is
     visible); a mouse reorders by dragging and pans with the wheel, the idioms
     each input already has. Order isn't rules-significant, so the drag target
     is a local-only $state array reconciled against the server's hand. -->
<script lang="ts">
	import { T } from "@threlte/core";
	import { HTML } from "@threlte/extras";
	import { storeGame, type Card } from "$stores/game.svelte";
	import { computeHandLine, centerSlotIndex } from "../layout/handLine";
	import { handSlotPose, HAND_STACK_STEP } from "../layout/handSlotPose";
	import { useCardBus } from "../card-bus.svelte";
	import { useCardRegistry } from "../animation/cardRegistry.svelte";
	import { neighborPushEm as falloffPushEm } from "../layout/handHoverFalloff";
	import { CARD_HEIGHT, CARD_WIDTH, EM_TO_WORLD } from "./units";
	import { loadTexture } from "./textures";
	import type { CameraRig } from "../layout/cameraRig";
	import type { BoardPlacement } from "../layout/boardPlacement";
	import type { ViewportInfo } from "../layout/seatLayout";
	import { pointerMode } from "../layout/pointerMode.svelte";
	import {
		pastDragThreshold,
		computeScrollEm,
		findNearestSlotIndex,
		computeReorderedIds
	} from "../layout/handGesture";
	import { devFixturePreset } from "../../../dev/devFixturePreset.svelte";
	import { ValueMap } from "$lib/generated/schemas";

	let {
		rig,
		viewport,
		placement,
		selectedId = null,
		onSelectionChange,
		onPlay,
		dimmed = false,
		focusedId = null,
		onPointerHover
	}: {
		rig: CameraRig;
		viewport: ViewportInfo;
		placement: BoardPlacement;
		selectedId?: number | null;
		onSelectionChange: (cardId: number | null) => void;
		/** Direct play, hover devices only — touch goes through the selection. */
		onPlay: (cardId: number) => void;
		/** Darkens the whole hand when it isn't this player's turn — see
		 *  Scene3D's DIM_LOCAL_WHEN_NOT_TURN for why this is a toggle. */
		dimmed?: boolean;
		/** The keyboard-focused card, from AccessibleHandControls — lifts/highlights
		 *  it the same way a mouse hover would, so Tab/Arrow navigation has the same
		 *  visible feedback a pointer already gets instead of the DOM-only focus a
		 *  screen-reader button would otherwise leave sighted keyboard users with. */
		focusedId?: number | null;
		/** A real pointer entered one of the hand's own hit zones — not fired by
		 *  the wheel handler's own programmatic hoveredId writes. Tells the owner
		 *  to drop any keyboard focus, so the mouse taking over doesn't leave a
		 *  keyboard-focused card lit at the same time as the hovered one. */
		onPointerHover?: () => void;
	} = $props();

	const bus = useCardBus();
	const cardRegistry = useCardRegistry();
	// Negative Z = away from the viewer, up the screen. The row sits flush with
	// the bottom edge, so a lifted card has to pop out over the playmat; pushing
	// it the other way would take it off-screen.
	const HOVER_PUSH_EM = -3;
	// Half a card, in the em units the slot solver works in — CARD_WIDTH is one
	// world unit and EM_TO_WORLD is derived from it, so this is --cardSize / 2.
	const CARD_HALF_WIDTH_EM = CARD_WIDTH / EM_TO_WORLD / 2;
	// The pick planes float above every card (including a lifted/dragged one, at
	// DRAG_LIFT) so they always win the raycast.
	const HIT_PLANE_Y = 1;
	// The row grows symmetrically from center, so its left edge heads for the
	// draw pile: cap the span where the leftmost card's edge would reach the
	// pile's right edge (pile half-width + card half-width = one scaled card)
	// plus a small gap.
	const HAND_PILE_GAP = 0.25;
	// The fade reads off the actual screen, not the layout's own span cap: the
	// draw pile pins maxHalfSpanEm well inside the true frustum edge (see
	// maxHalfSpanEm below), so keying the fade off THAT used to fade a normal
	// 7-card hand's end cards before they were anywhere near the edge of the
	// screen. The middle third of the screen's width is always fully opaque;
	// the outer two thirds fade linearly, reaching zero exactly where the
	// card's own outer edge would touch the true screen edge (where it'd be
	// clipped by the camera frustum anyway).
	const EDGE_FADE_INNER_FRACTION = 1 / 3;
	// Below this, a pointer gesture is a tap; above it, a drag. Without it every
	// tap on a touch screen registers a few pixels of travel and scrolls the row.
	const DRAG_THRESHOLD_PX = 6;
	// Wheel/trackpad panning is lock-stepped one card at a time rather than
	// smooth, so scrolling a long hand feels like flipping through it (and,
	// later, can click a notch per step) instead of a continuous slide. This
	// is the sensitivity knob: how many pixels of accumulated wheel motion
	// buy one step.
	const WHEEL_STEP_PX = 60;
	// Red, Green, Blue, Yellow, then wilds last (they have no color of their own).
	const RGBY_TYPE_ORDER = ["red", "green", "blue", "yellow", "white"];
	// Gap between the draw pile's own left edge and the sort button beside it.
	const SORT_BUTTON_GAP_EM = 3.2;
	// make room for it, in em, tapering off exponentially so only the
	// handful of cards nearest the active one actually move — the ends of
	// a long hand shouldn't shuffle just because something near the middle
	// got picked up.
	const HOVER_PUSH_PARAMS = { amplitudeEm: 1.5, decay: 2.5 };
	// The active card tilts a few degrees toward the discard pile at the mat's
	// center (x=0) — a small "already being aimed at where it's about to land"
	// cue — ramped in with the same liftT tween as its lift/push.
	const HOVER_TILT_DEG = 9;
	// Same recipe as the discard pile's own card shadows (DiscardPile3D): a
	// black-tinted copy of background.png at a low opacity. Passed to
	// CardMesh3D as local offsets in the card's own group space (see its
	// `shadow` prop) so the shadow is a genuine child of the card and rides
	// its full transform — lift, push, spin and scale alike — rather than
	// being tracked from outside. Offset purely along the card's own width
	// axis (not diagonal), aimed at this row's own light direction instead of
	// the pile's diagonal one.
	const SHADOW_OFFSET = 0.09;
	const SHADOW_OPACITY = 0.22;
	const SHADOW_DROP_Y = HAND_STACK_STEP / 2;

	// The placement's hand scale also scales the slot spacing and lift push so
	// the row's overlap proportions stay the same at any card size.
	let handEmToWorld = $derived(EM_TO_WORLD * placement.handScale);
	// Two limits, whichever is tighter: the draw pile on the left, the frustum's
	// own edge on the right (the row is centered on x=0, so the tighter of the
	// two bounds both sides). Once the pile has moved up onto the mat
	// (boardPlacement.ts's drawPileBesideHand) it no longer shares the row's
	// line, so only the frustum edge is left — which is the whole point of
	// moving it there.
	let edgeHalfSpan = $derived(rig.halfWidth - (CARD_WIDTH * placement.handScale) / 2);
	let maxHalfSpanEm = $derived(
		(placement.drawPileBesideHand
			? Math.min(
					Math.abs(placement.drawPileX) - CARD_WIDTH * placement.handScale - HAND_PILE_GAP,
					edgeHalfSpan
				)
			: edgeHalfSpan) / handEmToWorld
	);

	// The sort button tucks against the draw pile while the pile shares the hand
	// row. Once the pile moves onto the mat there is nothing to tuck against, so
	// the button takes the row's own left end — mirroring the overflow hint on
	// the right rather than floating in the gap the pile left behind.
	let sortButtonX = $derived(
		placement.drawPileBesideHand
			? placement.drawPileX - SORT_BUTTON_GAP_EM * handEmToWorld
			: -((maxHalfSpanEm + CARD_HALF_WIDTH_EM) * handEmToWorld + 0.3)
	);

	let cards = $derived(
		(storeGame.localPlayer?.hand ?? []).filter(
			(c) => c.id !== bus.pendingLocalPlayDrawnId && !bus.pendingLocalDrawIds.has(c.id)
		)
	);

	// Reconciled, not replaced: new card ids append at the end, missing ones
	// drop out, everything else keeps its current position — so a drag that's
	// mid-flight survives an unrelated state refresh.
	let orderIds = $state<number[]>([]);
	$effect(() => {
		const liveIds = cards.map((c) => c.id);
		const kept = orderIds.filter((id) => liveIds.includes(id));
		const added = liveIds.filter((id) => !kept.includes(id));
		const next = [...kept, ...added];
		if (next.length !== orderIds.length || next.some((id, i) => id !== orderIds[i])) {
			orderIds = next;
		}
	});

	$effect(() => {
		bus.setLocalHandSnapshot({ orderIds, scrollEm: line.scrollEm, maxHalfSpanEm });
	});

	// One registry entry per hand card, forever: a real entry never
	// disappears while the card exists. This effect keeps every
	// current hand card's pose-provider and decoration in sync with this
	// component's own live layout, and applies that layout immediately unless
	// CardRegistry says the card is currently mid-transition (a GSAP-owned
	// draw/play flight), in which case the flight owns the pose until it hands
	// back control on completion (CardRegistry.finishBeat).
	$effect(() => {
		const snapshot = { orderIds, scrollEm: line.scrollEm, maxHalfSpanEm };
		for (const [i, card] of orderedCards.entries()) {
			const idString = String(card.id);
			const isDragging = draggingId === card.id;
			const isSelected = selectedId === card.id;
			const slot = slots[i];
			const neighborPush = isDragging ? 0 : neighborPushEm(i);

			cardRegistry.ensureEntry(
				idString,
				(() => {
					const [x, y, z] = handSlotPose(i, orderedCards.length, snapshot, placement, { dragging: isDragging });
					return { x, y, z, spinDeg: 0, flipDeg: 0, scale: placement.handScale, turned: false, opacity: 1 };
				})(),
				{ type: card.type, value: card.value }
			);
			cardRegistry.setPoseProvider(idString, () =>
				handSlotPose(i, orderedCards.length, snapshot, placement, { dragging: isDragging })
			);
			cardRegistry.applyIdlePoseIfNotInTransit(idString);

			const lifted =
				isSelected || ((focusedId !== null ? focusedId === card.id : hoveredId === card.id) && !isDragging);
			const fade = edgeFade(slot.x);
			cardRegistry.setDecoration(idString, {
				hovered: lifted,
				instant: isSelected,
				hoverPush: [0, HOVER_PUSH_EM * handEmToWorld],
				pushX: neighborPush * handEmToWorld,
				hoverSpinDeg: tiltTowardPileDeg(slot.x),
				opacity: isSelected ? 1 : fade,
				dimmed,
				shadow: shadowTexture
					? { texture: shadowTexture, offsetX: SHADOW_OFFSET, dropZ: SHADOW_DROP_Y, opacity: SHADOW_OPACITY }
					: undefined,
				highlight: isSelected ? {} : undefined
			});
		}
	});

	// A card that left the hand (played) can't stay selected, or the discard
	// pile keeps offering to confirm something you no longer hold. Checked
	// against the server's own hand, not the hidden-filtered `cards` above —
	// a card mid-deal-in flight is only visually hidden, not actually gone,
	// and clearing the selection out from under it is what silently ate a
	// pre-armed selection for the whole draw-in animation window.
	let rawHandIds = $derived(new Set((storeGame.localPlayer?.hand ?? []).map((card) => card.id)));
	$effect(() => {
		if (selectedId !== null && !rawHandIds.has(selectedId)) {
			onSelectionChange(null);
		}
	});

	let orderedCards = $derived(
		orderIds.map((id) => cards.find((c) => c.id === id)).filter((c): c is Card => c !== undefined)
	);

	function sortByRgby() {
		orderIds = [...cards]
			.sort((a, b) => {
				const typeDelta = RGBY_TYPE_ORDER.indexOf(a.type) - RGBY_TYPE_ORDER.indexOf(b.type);
				if (typeDelta !== 0) return typeDelta;
				return ValueMap.indexOf(a.value) - ValueMap.indexOf(b.value);
			})
			.map((c) => c.id);
	}

	let scrollEm = $state(0);
	// Sub-step wheel/trackpad motion banked toward the next WHEEL_STEP_PX
	// threshold — not $state, the wheel handler is its only reader or writer.
	let wheelAccumPx = 0;
	let line = $derived(computeHandLine(orderedCards.length, maxHalfSpanEm, scrollEm));
	let slots = $derived(line.slots);
	let worldPerPixelX = $derived((2 * rig.halfWidth) / viewport.width);

	// Cards dissolve as they pan toward the screen's true edges instead of
	// being sliced off there — which also makes it visible that the row
	// scrolls. Measured in the same em units the row's own slots use, against
	// the frustum's real half-width converted through the current hand scale
	// (screenHalfSpanEm), not against maxHalfSpanEm — that cap exists to keep
	// the row clear of the draw pile, which is a much tighter box than the
	// screen itself.
	let screenHalfSpanEm = $derived(rig.halfWidth / handEmToWorld);
	function edgeFade(x: number): number {
		const innerEm = screenHalfSpanEm * EDGE_FADE_INNER_FRACTION;
		const cardOuterEdgeEm = Math.abs(x) + CARD_HALF_WIDTH_EM;
		if (cardOuterEdgeEm <= innerEm) return 1;
		const fadeSpanEm = screenHalfSpanEm - innerEm;
		const t = fadeSpanEm <= 0 ? 1 : Math.min(1, (cardOuterEdgeEm - innerEm) / fadeSpanEm);
		return 1 - t;
	}

	// The row can only ever be panned when it's actually wider than its
	// visible span — that's exactly what maxScrollEm measures.
	let hasOverflow = $derived(line.maxScrollEm > 0.01);

	// One pick zone per card, tiling the row edge-to-edge: a card's own visible
	// sliver is only as wide as the slot spacing and sits off to one side, so
	// picking by card geometry meant aiming at a few pixels between neighbors.
	// Each zone instead spans from the midpoint with the previous card to the
	// midpoint with the next one (the outermost two reaching out to the card's
	// real edge), which is the widest unambiguous target each card can own.
	let hitZones = $derived(
		slots.map((slot, i) => {
			const left = i > 0 ? (slots[i - 1].x + slot.x) / 2 : slot.x - CARD_HALF_WIDTH_EM;
			const right =
				i < slots.length - 1 ? (slot.x + slots[i + 1].x) / 2 : slot.x + CARD_HALF_WIDTH_EM;
			return { centerX: (left + right) / 2, widthEm: right - left };
		})
	);

	let hoveredId = $state<number | null>(devFixturePreset.hoverId);
	// Whether the pointer is actually over one of the hand's hit zones right
	// now — distinct from `hoveredId`, which the wheel handler below also
	// drives programmatically when the pointer isn't here at all. Without
	// this, a scroll-picked hoveredId would be mistaken for a real hover on
	// the next wheel tick and get carried forward instead of recentered.
	let pointerOverHand = $state(false);
	let draggingId = $state<number | null>(null);
	let dragOffsetX = $state(0);
	let dragIndex = 0;
	let gestureCardId: number | null = null;
	let gestureIsReorder = false;
	let gestureMoved = false;
	let pointerStartX = 0;
	let scrollStartEm = 0;

	function clientXOf(event: unknown): number {
		const e = event as { clientX?: number; nativeEvent?: { clientX?: number } };
		return e.nativeEvent?.clientX ?? e.clientX ?? 0;
	}

	// With a mouse, dragging always reorders — the wheel already pans, and the
	// cursor makes the grabbed card unambiguous. With a finger there is only the
	// one gesture to spend, so it pans, and reordering is what dragging the card
	// you already picked does.
	function startGesture(cardId: number, index: number, event: unknown) {
		gestureCardId = cardId;
		gestureIsReorder = pointerMode.canHover || selectedId === cardId;
		gestureMoved = false;
		dragIndex = index;
		dragOffsetX = 0;
		pointerStartX = clientXOf(event);
		scrollStartEm = line.scrollEm;
		window.addEventListener("pointermove", handleGestureMove);
		window.addEventListener("pointerup", endGesture, { once: true });
	}

	function handleGestureMove(event: PointerEvent) {
		if (gestureCardId === null) return;
		const deltaPx = event.clientX - pointerStartX;
		if (!gestureMoved && !pastDragThreshold(deltaPx, DRAG_THRESHOLD_PX)) return;
		gestureMoved = true;

		if (!gestureIsReorder) {
			// Content follows the finger: dragging right reveals the cards off the
			// left end, which is a decreasing scroll offset.
			scrollEm = computeScrollEm(scrollStartEm, deltaPx, worldPerPixelX, handEmToWorld);
			return;
		}

		hoveredId = null;
		draggingId = gestureCardId;
		dragOffsetX = deltaPx * worldPerPixelX;

		const draggedX = slots[dragIndex].x + dragOffsetX / handEmToWorld;
		const targetIndex = findNearestSlotIndex(slots, draggedX);

		if (targetIndex !== dragIndex) {
			orderIds = computeReorderedIds(orderIds, dragIndex, targetIndex);
			dragIndex = targetIndex;
			// re-anchor so the reshuffled order doesn't jump under the pointer
			pointerStartX = event.clientX;
			dragOffsetX = 0;
		}
	}

	// The pointer ends a gesture wherever it happens to be, which is rarely over
	// the zone it started on and often over no zone at all — so hover is cleared
	// here rather than waiting for a pointerleave that may never arrive. That
	// missing leave event is what used to strand a dragged card in its lifted pose.
	function endGesture() {
		if (gestureCardId !== null && !gestureMoved) {
			if (pointerMode.canHover) {
				onPlay(gestureCardId);
			} else {
				onSelectionChange(selectedId === gestureCardId ? null : gestureCardId);
			}
		}
		gestureCardId = null;
		gestureIsReorder = false;
		draggingId = null;
		dragOffsetX = 0;
		hoveredId = null;
		window.removeEventListener("pointermove", handleGestureMove);
	}

	// On `window`, not the row itself, so scrolling works no matter where the
	// pointer is — a card game's wheel scroll shouldn't require parking the
	// mouse over a specific strip first. Re-anchoring on the clamped
	// line.scrollEm rather than accumulating keeps the offset in range.
	$effect(() => {
		if (!pointerMode.canHover) return;
		const handleWheel = (event: WheelEvent) => {
			if (!hasOverflow) return;
			// A trackpad's two-finger swipe reports its own motion as deltaX; a
			// plain mouse wheel only ever reports deltaY. Taking whichever axis
			// moved further means a horizontal trackpad swipe pans the row
			// directly, instead of only the (unintuitive, but mouse-wheel-only)
			// vertical scroll the row used to require.
			const delta = Math.abs(event.deltaX) > Math.abs(event.deltaY) ? event.deltaX : event.deltaY;
			wheelAccumPx += delta;

			// Tracked locally rather than re-read from `line` each step — `line`
			// won't reflect this handler's own `scrollEm` writes until the next
			// render, so re-reading it mid-loop would just see the same clamped
			// value on every iteration.
			let tentativeScrollEm = line.scrollEm;
			const maxScrollEm = line.maxScrollEm;
			// Only meaningful while the pointer really is over the row — once the
			// wheel handler itself starts writing hoveredId (the pointerOverHand
			// === false branch below) this stays stale, which is fine, it's only
			// read in that same branch's opposite.
			const followingPointer = pointerOverHand && hoveredId !== null;

			while (Math.abs(wheelAccumPx) >= WHEEL_STEP_PX) {
				const step = Math.sign(wheelAccumPx);
				wheelAccumPx -= step * WHEEL_STEP_PX;

				const nextScrollEm = Math.max(
					-maxScrollEm,
					Math.min(maxScrollEm, tentativeScrollEm + step * line.spacingEm)
				);
				// Already at the scroll limit — the row didn't actually move this
				// step, so neither should the hover. Forwarding it anyway is what
				// used to lift a card the pointer was never over once scrolling
				// hit the edge.
				if (nextScrollEm === tentativeScrollEm) continue;
				tentativeScrollEm = nextScrollEm;

				// The row moved a whole card under a pointer that never itself
				// moved — carry the hover forward by the same step, or a wheel
				// scroll leaves it stranded on whichever card used to be there.
				if (followingPointer) {
					const hoveredIndex = orderedCards.findIndex((c) => c.id === hoveredId);
					const nextIndex = hoveredIndex + step;
					if (hoveredIndex !== -1 && nextIndex >= 0 && nextIndex < orderedCards.length) {
						hoveredId = orderedCards[nextIndex].id;
					}
				}
			}
			scrollEm = tentativeScrollEm;

			// The pointer isn't over the row (it might not even be over the
			// window), so there's no card for it to be "on" — instead, whatever
			// card ends up nearest screen-center becomes the highlighted one,
			// recomputed every tick so it tracks the scroll live instead of
			// snapping once the gesture ends.
			if (!followingPointer) {
				const centeredLine = computeHandLine(orderedCards.length, maxHalfSpanEm, tentativeScrollEm);
				const centerIndex = centerSlotIndex(centeredLine);
				hoveredId = centerIndex === -1 ? null : (orderedCards[centerIndex]?.id ?? null);
			}
		};
		window.addEventListener("wheel", handleWheel, { passive: true });
		return () => window.removeEventListener("wheel", handleWheel);
	});

	// The card that's currently lifted — selection wins outright, then a
	// keyboard focus (an explicit, discrete action) wins over a merely
	// resting mouse hover, or a stationary pointer left over a card from
	// before the keyboard took over would light up two cards at once. A drag
	// in progress isn't a "hover", or the row would part around a card
	// mid-reorder.
	let activeId = $derived(selectedId ?? (draggingId === null ? (focusedId ?? hoveredId) : null));
	let activeIndex = $derived(
		activeId === null ? -1 : orderedCards.findIndex((c) => c.id === activeId)
	);

	// Neighbors on the active card's side shift a sliver further away, tapering
	// off exponentially so only the ones actually crowding it move.
	function neighborPushEm(index: number): number {
		if (activeIndex === -1) return 0;
		return falloffPushEm(index, activeIndex, HOVER_PUSH_PARAMS);
	}

	// Tilts the active card's top edge toward the discard pile at x=0 — a card
	// left of center tips right (positive spin), one right of center tips left.
	function tiltTowardPileDeg(x: number): number {
		return x > 0 ? -HOVER_TILT_DEG : HOVER_TILT_DEG;
	}

	let shadowTexture = $state<import("three").Texture | null>(null);
	$effect(() => {
		let cancelled = false;
		loadTexture("/assets/cards/background.png").then((t) => {
			if (!cancelled) shadowTexture = t;
		});
		return () => {
			cancelled = true;
		};
	});
</script>

{#each orderedCards as card, i (card.id)}
	{@const zone = hitZones[i]}
	<T.Mesh
		position={[zone.centerX * handEmToWorld, HIT_PLANE_Y, placement.localSeatZ]}
		rotation.x={-Math.PI / 2}
		onpointerenter={() => {
			pointerOverHand = true;
			onPointerHover?.();
			if (draggingId === null) hoveredId = card.id;
		}}
		onpointerleave={() => {
			pointerOverHand = false;
			if (hoveredId === card.id) hoveredId = null;
		}}
		onpointerdown={(event: unknown) => startGesture(card.id, i, event)}
	>
		<T.PlaneGeometry args={[zone.widthEm * handEmToWorld, CARD_HEIGHT * placement.handScale]} />
		<T.MeshBasicMaterial transparent opacity={0} depthWrite={false} />
	</T.Mesh>
{/each}

{#if hasOverflow}
	<HTML
		position={[(maxHalfSpanEm + CARD_HALF_WIDTH_EM) * handEmToWorld + 0.3, 1, placement.localSeatZ]}
		center
		pointerEvents="none"
	>
		<svg class="overflow-hint" viewBox="0 0 24 24" width="26" height="26">
			<path
				d="M8 4 L16 12 L8 20"
				fill="none"
				stroke="white"
				stroke-width="3.5"
				stroke-linecap="round"
				stroke-linejoin="round"
			/>
		</svg>
	</HTML>
{/if}

<HTML
	position={[sortButtonX, 1, placement.localSeatZ]}
	center
	pointerEvents="auto"
>
	<button
		class="sort-button pixel-corners"
		onclick={sortByRgby}
		title="Sort hand"
		aria-label="Sort hand"
	>
		<i class="hn pix hn-sort"></i>
	</button>
</HTML>

<style>
	.overflow-hint {
		filter: drop-shadow(0 0 2px rgba(0, 0, 0, 0.8));
		opacity: 0.85;
	}

	/* Square: the glyph carries the meaning now, so the old text-width padding
	   would leave it floating off-center in a wide box. */
	.sort-button {
		display: grid;
		place-items: center;
		font-size: 1.1em;
		line-height: 1;
		padding: 0.4em;
		border: none;
		cursor: pointer;
		background: var(--table-chip);
		color: var(--table-text);
	}
</style>
