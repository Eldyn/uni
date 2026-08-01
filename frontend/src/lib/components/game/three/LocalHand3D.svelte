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
	import { computeHandLine } from "../layout/handLine";
	import { useCardBus } from "../card-bus.svelte";
	import CardMesh3D from "./CardMesh3D.svelte";
	import CardHighlight3D from "./CardHighlight3D.svelte";
	import { CARD_HEIGHT, CARD_WIDTH, EM_TO_WORLD, CARD_HOVER_LIFT, CARD_HOVER_SCALE } from "./units";
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
		dimmed = false
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
	} = $props();

	const bus = useCardBus();
	// Each card is itself a stack of layered planes (CardMesh3D's background/
	// value/border, offset up to 0.004 world units apart) — a per-card step
	// smaller than that lets one card's layers interleave with its neighbor's,
	// which is what caused the z-fighting. 0.02 clears that with margin while
	// staying a "micro" adjustment (even a 20-card hand only drifts 0.4 units).
	const STACK_STEP = 0.02;
	// Negative Z = away from the viewer, up the screen. The row sits flush with
	// the bottom edge, so a lifted card has to pop out over the playmat; pushing
	// it the other way would take it off-screen.
	const HOVER_PUSH_EM = -3;
	const DRAG_LIFT = 0.5;
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
	// One wheel notch pans the row by a card's width, so scrolling a long hand
	// feels like flipping through it rather than nudging it.
	const WHEEL_EM_PER_PIXEL = 0.02;
	// Red, Green, Blue, Yellow, then wilds last (they have no color of their own).
	const RGBY_TYPE_ORDER = ["red", "green", "blue", "yellow", "white"];
	// Gap between the draw pile's own left edge and the sort button beside it.
	const SORT_BUTTON_GAP_EM = 3.2;
	// How far the active (hovered/selected) card's immediate neighbors part to
	// make room for it, in em, tapering off over NEIGHBOR_PUSH_FALLOFF_CARDS so
	// only the handful of cards nearest the active one actually move — the ends
	// of a long hand shouldn't shuffle just because something near the middle
	// got picked up.
	const NEIGHBOR_PUSH_EM = 0.7;
	const NEIGHBOR_PUSH_FALLOFF_CARDS = 3;
	// The active card tilts a few degrees toward the discard pile at the mat's
	// center (x=0) — a small "already being aimed at where it's about to land"
	// cue — ramped in with the same liftT tween as its lift/push.
	const HOVER_TILT_DEG = 9;
	// Same recipe as the discard pile's own card shadows (DiscardPile3D): a
	// black-tinted copy of background.png at a low opacity, offset in WORLD
	// units — same fixed 0.09 magnitude as the pile's, not scaled by handScale,
	// so it reads as "the same shadow" rather than growing with the cards.
	// Offset purely left (not diagonal), aimed at this row's own light
	// direction instead of the pile's diagonal one.
	const SHADOW_OFFSET = 0.09;
	const SHADOW_OPACITY = 0.22;
	const SHADOW_DROP_Y = STACK_STEP / 2;

	// The placement's hand scale also scales the slot spacing and lift push so
	// the row's overlap proportions stay the same at any card size.
	let handEmToWorld = $derived(EM_TO_WORLD * placement.handScale);
	// Two limits, whichever is tighter: the draw pile on the left, the frustum's
	// own edge on the right (the row is centered on x=0, so the tighter of the
	// two bounds both sides).
	let maxHalfSpanEm = $derived(
		Math.min(
			Math.abs(placement.drawPileX) - CARD_WIDTH * placement.handScale - HAND_PILE_GAP,
			rig.halfWidth - (CARD_WIDTH * placement.handScale) / 2
		) / handEmToWorld
	);

	let cards = $derived(
		(storeGame.localPlayer?.hand ?? []).filter((card) => !bus.hiddenCardIds.has(card.id))
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

	// A card that left the hand (played, or flying) can't stay selected, or the
	// discard pile keeps offering to confirm something you no longer hold.
	$effect(() => {
		if (selectedId !== null && !cards.some((card) => card.id === selectedId)) {
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

	// Scoped to the row by what the pointer is over rather than by a DOM
	// listener: the whole scene is one canvas element, so there is no
	// hand-shaped node to hang a wheel handler on. Re-anchoring on the clamped
	// line.scrollEm rather than accumulating keeps the offset in range.
	$effect(() => {
		if (!pointerMode.canHover) return;
		const handleWheel = (event: WheelEvent) => {
			if (hoveredId === null) return;
			// A trackpad's two-finger swipe reports its own motion as deltaX; a
			// plain mouse wheel only ever reports deltaY. Taking whichever axis
			// moved further means a horizontal trackpad swipe pans the row
			// directly, instead of only the (unintuitive, but mouse-wheel-only)
			// vertical scroll the row used to require.
			const delta = Math.abs(event.deltaX) > Math.abs(event.deltaY) ? event.deltaX : event.deltaY;
			scrollEm = line.scrollEm + delta * WHEEL_EM_PER_PIXEL;
		};
		window.addEventListener("wheel", handleWheel, { passive: true });
		return () => window.removeEventListener("wheel", handleWheel);
	});

	// The card that's currently lifted — selection wins over hover (matches
	// `lifted` per-card below), and a drag in progress isn't a "hover", or the
	// row would part around a card mid-reorder.
	let activeId = $derived(selectedId ?? (draggingId === null ? hoveredId : null));
	let activeIndex = $derived(
		activeId === null ? -1 : orderedCards.findIndex((c) => c.id === activeId)
	);

	// Neighbors on the active card's side shift a sliver further away, tapering
	// off over a few cards so only the ones actually crowding it move.
	function neighborPushEm(index: number): number {
		if (activeIndex === -1) return 0;
		const distance = index - activeIndex;
		if (distance === 0) return 0;
		const magnitude =
			NEIGHBOR_PUSH_EM * Math.max(0, 1 - (Math.abs(distance) - 1) / NEIGHBOR_PUSH_FALLOFF_CARDS);
		return Math.sign(distance) * magnitude;
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
	{@const slot = slots[i]}
	{@const isDragging = draggingId === card.id}
	{@const isSelected = selectedId === card.id}
	{@const neighborPush = isDragging ? 0 : neighborPushEm(i)}
	{@const x = (slot.x + neighborPush) * handEmToWorld + (isDragging ? dragOffsetX : 0)}
	{@const lifted = isSelected || (hoveredId === card.id && !isDragging)}
	{@const fade = edgeFade(slot.x)}
	{#if isSelected}
		<!-- Sits at the lifted card's own pose, one step under it, so the rim reads
		     as an outline on the card rather than a plate on the row. -->
		<CardHighlight3D
			position={[
				x,
				i * STACK_STEP + CARD_HOVER_LIFT - 0.01,
				placement.localSeatZ + HOVER_PUSH_EM * handEmToWorld
			]}
			scale={placement.handScale * CARD_HOVER_SCALE}
		/>
	{/if}
	{#if shadowTexture}
		<!-- Same recipe as the discard pile's card shadows, aimed left instead of
		     the pile's own diagonal — see SHADOW_OFFSET above. -->
		<T.Mesh
			position={[
				x - SHADOW_OFFSET,
				(isDragging ? DRAG_LIFT : i * STACK_STEP) - SHADOW_DROP_Y,
				placement.localSeatZ
			]}
			rotation.x={-Math.PI / 2}
			scale={placement.handScale}
		>
			<T.PlaneGeometry args={[CARD_WIDTH, CARD_HEIGHT]} />
			<T.MeshBasicMaterial
				map={shadowTexture}
				color="#000000"
				transparent
				opacity={SHADOW_OPACITY * fade}
				depthWrite={false}
				toneMapped={false}
			/>
		</T.Mesh>
	{/if}
	<CardMesh3D
		{card}
		position={[x, isDragging ? DRAG_LIFT : i * STACK_STEP, placement.localSeatZ]}
		scale={placement.handScale}
		hovered={lifted}
		instant={isSelected}
		hoverPush={[0, HOVER_PUSH_EM * handEmToWorld]}
		hoverSpinDeg={tiltTowardPileDeg(slot.x)}
		opacity={fade}
		{dimmed}
	/>
{/each}

{#each orderedCards as card, i (card.id)}
	{@const zone = hitZones[i]}
	<T.Mesh
		position={[zone.centerX * handEmToWorld, HIT_PLANE_Y, placement.localSeatZ]}
		rotation.x={-Math.PI / 2}
		onpointerenter={() => draggingId === null && (hoveredId = card.id)}
		onpointerleave={() => hoveredId === card.id && (hoveredId = null)}
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
	position={[placement.drawPileX - SORT_BUTTON_GAP_EM * handEmToWorld, 1, placement.localSeatZ]}
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
