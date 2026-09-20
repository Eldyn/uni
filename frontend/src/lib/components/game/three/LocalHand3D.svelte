<!-- The local player's own hand: a straight, slightly overlapping row of
     cards (layout/handLine.ts), top-down like the rest of the board.

     How you play a card:

       mouse — point at a card to preview it lifted, click to play it (one
               step, because the hover already showed you what you were
               committing to). You can also pick a card up and drop it on the
               discard pile to play it.
       touch — tap the card to play it. A drag picks the card up instead: a
               predominantly vertical drag lifts it so it can be dropped on
               the discard pile (or reordered in the row), while a
               predominantly horizontal drag scrolls the hand. Holding a
               picked-up card against a screen edge also auto-scrolls the row,
               on both touch and mouse, so off-screen cards stay reachable.

     A hand too wide for the screen stops compressing and becomes a scrollable
     strip: cards fade out at the ends rather than being cut off mid-card.
     Order isn't rules-significant, so the drag target is a local-only $state
     array reconciled against the server's hand. -->
<script lang="ts">
	import { onDestroy } from "svelte";
	import { T, useTask } from "@threlte/core";
	import { HTML } from "@threlte/extras";
	import { gsap } from "gsap";
	import { storeGame, type Card, type GamePlayer } from "$stores/game.svelte";
	import { storeAnimation } from "$stores/animation.svelte";
	import { storeRenderSettings } from "$stores/renderSettings.svelte";
	import { computeHandLine, centerSlotIndex } from "../layout/handLine";
	import { handSlotPose, HAND_STACK_STEP, DRAG_LIFT } from "../layout/handSlotPose";
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
		findReorderTargetIndex,
		computeReorderedIds,
		decideGestureAxis,
		isOverDiscard,
		edgeScrollDirection
	} from "../layout/handGesture";
	import { MAX_JITTER_EM } from "../layout/discardPile";
	import { devFixturePreset } from "../../../dev/devFixturePreset.svelte";
	import { ValueMap } from "$lib/generated/schemas";
	import { hiddenBackCountFor, hiddenBackCard } from "../layout/spectatorPov";
	import { storeCardDetail } from "$stores/cardDetail.svelte";
	import type { HandMorph } from "$stores/tableSpin.svelte";

	let {
		rig,
		viewport,
		placement,
		selectedId = null,
		onSelectionChange,
		onPlay,
		dimmed = false,
		focusedId = null,
		onPointerHover,
		player = null,
		readOnly = false,
		handMorph = null,
		inheritProgress = 1
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
		/** Whose hand this row renders. Defaults to the local player; Scene3D
		 *  passes a spectator's viewed player so their hand becomes the bottom row. */
		player?: GamePlayer | null;
		/** Spectator POV: render only. The row's hit zones, drag/reorder gesture,
		 *  wheel pan and sort button are all inert — a spectator can never play,
		 *  drag or reorder the viewed player's cards. */
		readOnly?: boolean;
		/** Spectator POV spin, inherit phase: the incoming viewed player's hand
		 *  snapshot to morph into this row. Set only when this row's `player` is
		 *  the incoming POV; null otherwise. Mirrors PlayerSeat3D's `ringMorph`. */
		handMorph?: HandMorph | null;
		/** Inherit-phase progress, 0 → 1. Blends a morphed card from its
		 *  `handMorph` arc source to its normal hand-row pose. */
		inheritProgress?: number;
	} = $props();

	// The prop is authoritative (Scene3D always passes it); the fallback keeps
	// the component self-contained if it's ever used standalone.
	let handPlayer = $derived(player ?? storeGame.localPlayer);

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
	// pile's right edge (pile half-width + card half-width) plus a small gap.
	// The pile is drawn at its own scale, smaller than the hand in portrait.
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
	// How wide a band at each screen edge counts as "hold here to scroll" while
	// a card is picked up. Sized for a finger, not a cursor.
	const EDGE_SCROLL_BAND_PX = 72;
	// Auto-scroll speed while a held card sits in an edge band, in cards per
	// second. Expressed in cards rather than em so the feel doesn't change with
	// the solved hand scale.
	const EDGE_SCROLL_CARDS_PER_SEC = 5;
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
	const DRAG_SHADOW_OFFSET = 0.14;
	const DRAG_SHADOW_DROP_Y = 0.2;
	const DRAG_SHADOW_OPACITY = 0.28;
	// Multiplier on the discard pile's own footprint (half a card plus its
	// scatter) for how close a dragged card must get to count as dropped on it.
	// Deliberately generous: a finger covers more than a card center, and a near
	// miss that silently re-sorts the hand instead of playing is the worst
	// failure mode here.
	const DISCARD_DROP_FORGIVENESS = 1.25;

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
					Math.abs(placement.drawPileX) -
						(CARD_WIDTH * (placement.handScale + placement.drawPileScale)) / 2 -
						HAND_PILE_GAP,
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

	let handCards = $derived(handPlayer?.hand ?? []);
	// A spectator viewing a player whose hand the server withheld (privacy_mode)
	// still gets a full-looking row — of card backs, one per card_count, never
	// inferred faces. Only ever non-zero for a spectator: the local player's own
	// hand is always sent in full.
	let hiddenBackCount = $derived(readOnly ? hiddenBackCountFor(handPlayer) : 0);

	let cards = $derived.by(() => {
		if (hiddenBackCount > 0) {
			return Array.from({ length: hiddenBackCount }, (_, i) => hiddenBackCard(i));
		}
		// Same pending-draw/play hiding as the local player: baseBeats now treats
		// the spectator's viewed player as "local" too, so a card it is
		// animating into the POV row must not be planted already-revealed here.
		return handCards.filter(
			(c) => c.id !== bus.pendingLocalPlayDrawnId && !bus.pendingLocalDrawIds.has(c.id)
		);
	});

	// Synthetic facedown ids have no other owner to inherit them when the row
	// shrinks or the viewed player changes — unlike a real card, which
	// DiscardPile3D takes over after a play — so this row retires them itself.
	// Without this, switching between spectators' POVs (or a viewed player
	// playing a card while hidden) would strand back meshes on the board.
	let prevHiddenIds: number[] = [];
	$effect(() => {
		const currentIds = hiddenBackCount > 0 ? cards.map((c) => c.id) : [];
		for (const id of prevHiddenIds) {
			if (!currentIds.includes(id)) cardRegistry.removeEntry(String(id));
		}
		prevHiddenIds = currentIds;
	});

	// Real hand cards normally re-own the moment they leave the row: a play
	// hands the card to DiscardPile3D. But when a spectator switches the viewed
	// player, the outgoing player's cards leave this row with no new owner and
	// would strand as ghost faces at the bottom of the board. Retire ids that
	// left the row, aren't mid-flight (an animation/discard handoff owns those),
	// and never reached the discard (a played card is in transit first, then in
	// history). Synthetic backs are covered by the effect above.
	let prevRealIds: string[] = [];
	$effect(() => {
		const liveIds = new Set(hiddenBackCount > 0 ? [] : cards.map((c) => String(c.id)));
		const discardIds = new Set(bus.discardHistory.map((entry) => String(entry.card.id)));
		for (const id of prevRealIds) {
			if (liveIds.has(id)) continue;
			if (cardRegistry.isInTransit(id)) continue;
			if (discardIds.has(id)) continue;
			cardRegistry.removeEntry(id);
		}
		prevRealIds = [...liveIds];
	});

	// Unmount is the one case the two id-change retirement effects above cannot
	// cover: the component is gone, so nothing observes its keys leaving. Retire
	// every key this row currently owns — real ids and the synthetic hidden-back
	// ids alike — with the same guards as those effects: a card mid-flight (a
	// draw/play animation owns it) or one that reached the discard must not be
	// deleted out from under that owner. Mirrors PlayerSeat3D's onDestroy.
	onDestroy(() => {
		bus.setDraggingOverDiscard(false);
		const discardIds = new Set(bus.discardHistory.map((entry) => String(entry.card.id)));
		for (const key of [...prevRealIds, ...prevHiddenIds.map(String)]) {
			if (cardRegistry.isInTransit(key)) continue;
			if (discardIds.has(key)) continue;
			cardRegistry.removeEntry(key);
		}
	});

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

	let draggingId = $state<number | null>(null);
	let dragStartCardWorldX = 0;
	let dragStartCardWorldZ = 0;
	let dragWorldX = $state(0);
	let dragWorldZ = $state(0);
	let dragIndex = 0;
	let settlingCardId: string | null = null;
	const displacementTweens = new Map<string, gsap.core.Tween>();
	let prevOrderIds: number[] = [];
	let dragLiftTween: gsap.core.Tween | null = null;
	// Which player's incoming morph the previous effect run saw, and the card
	// keys that morph put in-transit. Together they detect the morph ending (or
	// switching player) and release exactly this hand's keys — mirroring
	// PlayerSeat3D's `prevMorphUsername` / `registeredKeys` release effect.
	let prevMorphUsername: string | null = null;
	const morphRegisteredKeys = new Set<string>();

	$effect(() => {
		return () => {
			for (const tween of displacementTweens.values()) {
				tween.kill();
			}
			displacementTweens.clear();
			settleTween?.kill();
			dragLiftTween?.kill();
		};
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
		const prevOrderIdsChanged =
			prevOrderIds.length === orderIds.length &&
			prevOrderIds.some((id, idx) => id !== orderIds[idx]);

		// Spectator POV spin, inherit phase. When this row is the incoming POV
		// player's hand, its cards seed at handMorph's arc source poses (the old
		// ring arrangement) and blend to the normal hand-row pose over
		// inheritProgress. handMorph.open (faces known) flips a card back → face
		// across the blend; hidden hands and bots arrive open:false and stay
		// backs — faces are never inferred.
		const morph = morphing ? handMorph : null;
		const morphProgress = morph ? inheritProgress : 1;
		const opening = morph !== null && morph.open;

		// Deterministic morph-end release, mirroring PlayerSeat3D. The controller
		// nulls `transition` (whence handMorph is derived) in the same synchronous
		// update that sets inheritProgress = 1, so no flush ever observes progress
		// 1 with a live morph — the in-loop release would never fire. Releasing
		// the instant the morph goes inactive is what clears `inTransit`;
		// otherwise the card stays in-transit with no displacement tween (the
		// morph killed it), making `isFlightTransit` true forever and permanently
		// skipping the idle re-sync.
		const morphUsername = morph?.username ?? null;
		if (prevMorphUsername !== null && prevMorphUsername !== morphUsername) {
			for (const key of morphRegisteredKeys) {
				// The morph killed any displacement tween, so there's normally
				// none to clobber; guard anyway so a live tween keeps ownership
				// of the pose and clears the flag itself on completion.
				if (!displacementTweens.has(key)) cardRegistry.markInTransit(key, false);
			}
			morphRegisteredKeys.clear();
		}
		prevMorphUsername = morphUsername;

		const currentCardIdSet = new Set(orderedCards.map((c) => String(c.id)));
		for (const [id, tween] of displacementTweens.entries()) {
			if (!currentCardIdSet.has(id)) {
				tween.kill();
				displacementTweens.delete(id);
			}
		}

		for (const [i, card] of orderedCards.entries()) {
			const idString = String(card.id);
			const isDragging = draggingId === card.id;
			const isSelected = selectedId === card.id;
			const slot = slots[i];
			const neighborPush = isDragging ? 0 : neighborPushEm(i);

			const [slotX, slotY, slotZ] = handSlotPose(i, orderedCards.length, snapshot, placement, {
				dragging: isDragging
			});
			// This card's arc source for the incoming morph. Out-of-range falls
			// back to the normal hand pose (source === target → the blend is a
			// no-op for that card rather than a snap to the world origin).
			const morphSource: [number, number, number] | null = morph ? (morph.poses[i] ?? null) : null;
			// Incoming morph, in-plane turn. The source is the ring slot's own
			// orientation, the target is the row's 0; an open morph also turns
			// the card in from back (flipDeg 180) to face (0) across the blend.
			const morphSpinSource = morph ? (morph.spinDegs?.[i] ?? 0) : 0;
			const entryFlipDeg = morph !== null && opening ? 180 * (1 - morphProgress) : 0;

			const entryPose = cardRegistry.ensureEntry(
				idString,
				{
					x: isDragging ? dragWorldX : slotX,
					y: isDragging ? DRAG_LIFT : slotY,
					z: isDragging ? dragWorldZ : slotZ,
					spinDeg: morph ? morphSpinSource * (1 - morphProgress) : 0,
					flipDeg: entryFlipDeg,
					scale: placement.handScale,
					turned: morph ? !opening : hiddenBackCount > 0,
					opacity: 1,
					dragT: isDragging ? 1 : 0
				},
				hiddenBackCount > 0 ? null : { type: card.type, value: card.value }
			);

			cardRegistry.setPoseProvider(idString, () => {
				const [x, y, z] = handSlotPose(i, orderedCards.length, snapshot, placement, {
					dragging: isDragging
				});
				const targetX = isDragging ? dragWorldX : x;
				const targetY = isDragging ? DRAG_LIFT : y;
				const targetZ = isDragging ? dragWorldZ : z;
				if (!morphSource) return [targetX, targetY, targetZ];
				const [sx, sy, sz] = morphSource;
				return [
					sx + (targetX - sx) * morphProgress,
					sy + (targetY - sy) * morphProgress,
					sz + (targetZ - sz) * morphProgress
				];
			});

			if (morph) {
				// Drive the blend straight off morphProgress every run. The
				// displacement tween would otherwise fight it (both write
				// pose.x/y/z from a different target), so kill it once and stay
				// in-transit until the blend lands — that also stops the idle
				// re-sync from snapping the card to its hand pose before the arc
				// has actually arrived.
				displacementTweens.get(idString)?.kill();
				displacementTweens.delete(idString);

				const [sx, sy, sz] = morphSource ?? [slotX, slotY, slotZ];
				entryPose.x = sx + (slotX - sx) * morphProgress;
				entryPose.y = sy + (slotY - sy) * morphProgress;
				entryPose.z = sz + (slotZ - sz) * morphProgress;
				entryPose.scale = placement.handScale;
				// Continuous across the blend, not a texture swap at 0.5: the
				// card turns back → face over the whole inherit, and spins from
				// the ring slot's orientation to the row's 0 at the same time.
				entryPose.turned = !opening;
				entryPose.flipDeg = opening ? 180 * (1 - morphProgress) : 0;
				entryPose.spinDeg = morphSpinSource * (1 - morphProgress);
				cardRegistry.markInTransit(idString, true);
				morphRegisteredKeys.add(idString);
			} else {
				// ensureEntry is idempotent, so a card created before a resize
				// keeps its draw-time scale forever unless the apply path
				// re-syncs it — mirror PlayerSeat3D's own
				// `if (!isInTransit) pose.scale = ...`. This is the base scale
				// only; drag/lift scale lives in dragT/liftT (CardMesh3D), so it
				// never fights an in-flight gesture.
				if (!cardRegistry.isInTransit(idString)) {
					entryPose.scale = placement.handScale;
				}

				if (isDragging) {
					const pose = cardRegistry.getPose(idString);
					if (pose) {
						pose.x = dragWorldX;
						pose.y = DRAG_LIFT;
						pose.z = dragWorldZ;
					}
				} else {
					const pose = cardRegistry.getPose(idString);
					if (pose) {
						// X alone misses a resolution change that moves the row in
						// depth without changing any slot's X: a height-only resize
						// (the mobile URL bar appearing, an orientation flip to a
						// same-width frame) leaves every slotX identical while
						// localSeatZ shifts by ~1.8 world units. Keying off X alone
						// started no tween and let applyIdlePoseIfNotInTransit write
						// the new Z in one frame — the card teleported. Measure the
						// full horizontal displacement, exactly as PlayerSeat3D does.
						const moved = Math.hypot(pose.x - slotX, pose.z - slotZ);
						const isFlightTransit =
							cardRegistry.isInTransit(idString) && !displacementTweens.has(idString);

						if (!isFlightTransit) {
							if (moved > 0.01) {
								displacementTweens.get(idString)?.kill();
								const duration = storeAnimation.enabled
									? 0.22 / Math.max(0.1, storeAnimation.speedMultiplier)
									: 0;
								if (duration === 0) {
									pose.x = slotX;
									pose.y = slotY;
									pose.z = slotZ;
									cardRegistry.markInTransit(idString, false);
									displacementTweens.delete(idString);
								} else {
									cardRegistry.markInTransit(idString, true);
									const tween = gsap.to(pose, {
										x: slotX,
										y: slotY,
										z: slotZ,
										duration,
										ease: "power2.out",
										onComplete: () => {
											displacementTweens.delete(idString);
											cardRegistry.markInTransit(idString, false);
											cardRegistry.applyIdlePoseIfNotInTransit(idString);
										}
									});
									displacementTweens.set(idString, tween);
								}
							} else if (!cardRegistry.isInTransit(idString)) {
								cardRegistry.applyIdlePoseIfNotInTransit(idString);
							}
						}
					}
				}
			}

			const lifted =
				isSelected ||
				((focusedId !== null ? focusedId === card.id : hoveredId === card.id) && !isDragging);
			const fade = edgeFade(slot.x);
			cardRegistry.setDecoration(idString, {
				tableBound: false,
				hovered: lifted,
				instant: isSelected,
				hoverPush: [0, HOVER_PUSH_EM * handEmToWorld],
				pushX: neighborPush * handEmToWorld,
				hoverSpinDeg: isDragging ? dragTiltDeg : lifted ? tiltTowardPileDeg(slot.x) : 0,
				opacity: isSelected ? 1 : fade,
				dimmed,
				shadow: shadowTexture
					? {
							texture: shadowTexture,
							offsetX: SHADOW_OFFSET,
							dropZ: SHADOW_DROP_Y,
							opacity: SHADOW_OPACITY
						}
					: undefined,
				highlight: isSelected ? {} : undefined
			});
		}

		prevOrderIds = [...orderIds];
	});

	// A card that left the hand (played) can't stay selected, or the discard
	// pile keeps offering to confirm something you no longer hold. Checked
	// against the server's own hand, not the hidden-filtered `cards` above —
	// a card mid-deal-in flight is only visually hidden, not actually gone,
	// and clearing the selection out from under it is what silently ate a
	// pre-armed selection for the whole draw-in animation window.
	let rawHandIds = $derived(new Set((handPlayer?.hand ?? []).map((card) => card.id)));
	$effect(() => {
		if (selectedId !== null && !rawHandIds.has(selectedId)) {
			onSelectionChange(null);
		}
	});

	let orderedCards = $derived(
		orderIds.map((id) => cards.find((c) => c.id === id)).filter((c): c is Card => c !== undefined)
	);

	// True while this row is the incoming POV player's hand and there are cards
	// to blend. `handMorph` is only ever non-null for the matching player (see
	// Scene3D), but the username check keeps the component self-contained.
	let morphing = $derived(
		handMorph !== null && handMorph.username === handPlayer?.username && orderedCards.length > 0
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
	let worldPerPixelZ = $derived((2 * rig.halfHeight) / viewport.height);
	// The discard pile's own reach, from the same half-card + scatter formula
	// boardPlacement uses to lay it out — how near a dragged card counts as
	// dropped on it.
	let discardDropRadius = $derived(
		((CARD_WIDTH * placement.centerScale) / 2 +
			MAX_JITTER_EM * EM_TO_WORLD * placement.centerScale) *
			DISCARD_DROP_FORGIVENESS
	);

	$effect(() => {
		if (bus.handScrollRequest !== null) {
			const target = bus.handScrollRequest;
			bus.handScrollRequest = null;
			scrollEm = Math.max(-line.maxScrollEm, Math.min(line.maxScrollEm, target));
		}
	});

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
	let gestureCardId: number | null = null;
	// Touch only: null until the first threshold-crossing movement decides
	// whether this drag scrolls the row or picks the card up (decideGestureAxis).
	// Hover devices seed it straight to "pickup" — the cursor already shows the
	// card and the wheel covers scrolling, so there is nothing to disambiguate.
	let gestureMode: "scroll" | "pickup" | null = null;
	let gestureMoved = false;
	// Whether the card being dragged is currently over the discard pile as a
	// valid drop. Mirrored onto the bus for the pile's highlight; read here on
	// release to decide play-vs-settle.
	let dragOverDiscard = false;
	let pointerStartX = 0;
	let pointerStartY = 0;
	let scrollStartEm = 0;
	let movementDelta = 0;
	let dragTiltDeg = $state(0);
	let lastPointerX = 0;
	let currentPointerX = 0;
	let hasPointerMove = false;

	useTask((delta) => {
		if (draggingId !== null) {
			// Edge auto-scroll: holding the picked-up card against a screen edge
			// pans the row so off-screen cards can be reached without dropping
			// the card. Runs every frame (not just on pointermove) so it keeps
			// scrolling while the finger rests still in the band.
			if (storeRenderSettings.autoScrollOnEdgeCreep && hasOverflow) {
				const dir = edgeScrollDirection(currentPointerX, viewport.width, EDGE_SCROLL_BAND_PX);
				if (dir !== 0) {
					const max = line.maxScrollEm;
					const step = dir * line.spacingEm * EDGE_SCROLL_CARDS_PER_SEC * delta;
					scrollEm = Math.max(-max, Math.min(max, scrollEm + step));
				}
			}

			const frameDelta = hasPointerMove ? (currentPointerX - lastPointerX) * worldPerPixelX : 0;
			lastPointerX = currentPointerX;
			hasPointerMove = false;

			// Balatro: lerp at speed ~25/frame toward current frame's actual pointer delta
			const factor = Math.min(1, delta * 25);
			movementDelta += (frameDelta - movementDelta) * factor;

			// rotationZ = movementDelta * 20 (clamped to [-30, 30] deg)
			dragTiltDeg = Math.max(-30, Math.min(30, movementDelta * 20));

			const handle = cardRegistry.activeFlights.find((h) => h.id === String(draggingId));
			if (handle?.decoration) {
				handle.decoration.hoverSpinDeg = dragTiltDeg;
			}
		} else if (movementDelta !== 0 || dragTiltDeg !== 0) {
			const factor = Math.min(1, delta * 25);
			movementDelta += (0 - movementDelta) * factor;
			dragTiltDeg = Math.max(-30, Math.min(30, movementDelta * 20));
			if (Math.abs(dragTiltDeg) < 0.01) {
				movementDelta = 0;
				dragTiltDeg = 0;
			}
		}
	});

	function clientXOf(event: unknown): number {
		const e = event as { clientX?: number; nativeEvent?: { clientX?: number } };
		return e.nativeEvent?.clientX ?? e.clientX ?? 0;
	}

	function clientYOf(event: unknown): number {
		const e = event as { clientY?: number; nativeEvent?: { clientY?: number } };
		return e.nativeEvent?.clientY ?? e.clientY ?? 0;
	}

	function preventDefaultOf(event: unknown): void {
		const e = event as {
			preventDefault?: () => void;
			nativeEvent?: { preventDefault?: () => void };
		};
		e.nativeEvent?.preventDefault?.();
		e.preventDefault?.();
	}

	function buttonOf(event: unknown): number {
		const e = event as { button?: number; nativeEvent?: { button?: number } };
		return e.nativeEvent?.button ?? e.button ?? 0;
	}

	let settleTween: gsap.core.Tween | null = null;

	// With a mouse, dragging always picks the card up: the wheel already pans,
	// and the cursor makes the grabbed card unambiguous. With a finger there is
	// only the one pointer to spend, so the axis of the first movement decides —
	// horizontal scrolls the row, vertical picks the card up (see
	// handleGestureMove and decideGestureAxis).
	function startGesture(cardId: number, index: number, event: unknown) {
		if (readOnly) return;
		// Right-click is inspection, not a gesture — starting one would let the
		// matching pointerup select or play the card under the context menu.
		if (buttonOf(event) !== 0) return;
		preventDefaultOf(event);
		storeCardDetail.resetLongPress();
		settleTween?.kill();
		settleTween = null;
		dragLiftTween?.kill();
		dragLiftTween = null;
		if (settlingCardId) {
			cardRegistry.markInTransit(settlingCardId, false);
			cardRegistry.applyIdlePoseIfNotInTransit(settlingCardId);
			settlingCardId = null;
		}
		gestureCardId = cardId;
		bus.setDraggingOverDiscard(false);
		dragOverDiscard = false;
		gestureMode = pointerMode.canHover ? "pickup" : null;
		gestureMoved = false;
		dragIndex = index;
		dragStartCardWorldX = slots[index] ? slots[index].x * handEmToWorld : 0;
		dragStartCardWorldZ = placement.localSeatZ;
		dragWorldX = dragStartCardWorldX;
		dragWorldZ = dragStartCardWorldZ;
		pointerStartX = clientXOf(event);
		pointerStartY = clientYOf(event);
		lastPointerX = pointerStartX;
		currentPointerX = pointerStartX;
		hasPointerMove = false;
		movementDelta = 0;
		dragTiltDeg = 0;
		scrollStartEm = line.scrollEm;
		window.addEventListener("pointermove", handleGestureMove);
		window.addEventListener("pointerup", endGesture, { once: true });
	}

	function handleGestureMove(event: PointerEvent) {
		if (gestureCardId === null) return;
		currentPointerX = event.clientX;
		hasPointerMove = true;
		const deltaPx = event.clientX - pointerStartX;
		const deltaPy = event.clientY - pointerStartY;
		if (!gestureMoved && Math.hypot(deltaPx, deltaPy) < DRAG_THRESHOLD_PX) return;
		gestureMoved = true;

		// Touch: this first threshold-crossing movement is what decides the
		// gesture's intent, then it's locked for the rest of the drag.
		if (gestureMode === null) {
			gestureMode = decideGestureAxis(deltaPx, deltaPy);
		}

		if (gestureMode === "scroll") {
			// Content follows the finger: dragging right reveals the cards off the
			// left end, which is a decreasing scroll offset.
			scrollEm = computeScrollEm(scrollStartEm, deltaPx, worldPerPixelX, handEmToWorld);
			return;
		}

		hoveredId = null;
		draggingId = gestureCardId;

		const rawWorldX = dragStartCardWorldX + deltaPx * worldPerPixelX;
		const rawWorldZ = dragStartCardWorldZ + deltaPy * worldPerPixelZ;

		const minWorldX = -rig.halfWidth + (CARD_WIDTH * placement.handScale) / 2;
		const maxWorldX = rig.halfWidth - (CARD_WIDTH * placement.handScale) / 2;
		const minWorldZ = rig.centerZ - rig.halfHeight + (CARD_HEIGHT * placement.handScale) / 2;
		const maxWorldZ = rig.centerZ + rig.halfHeight - (CARD_HEIGHT * placement.handScale) / 2;

		dragWorldX = Math.max(minWorldX, Math.min(maxWorldX, rawWorldX));
		dragWorldZ = Math.max(minWorldZ, Math.min(maxWorldZ, rawWorldZ));

		const pose = cardRegistry.getPose(String(gestureCardId));
		if (pose) {
			pose.x = dragWorldX;
			pose.y = DRAG_LIFT;
			pose.z = dragWorldZ;
			if (!dragLiftTween && (pose.dragT === undefined || pose.dragT < 1)) {
				const dragDuration = storeAnimation.enabled
					? 0.15 / Math.max(0.1, storeAnimation.speedMultiplier)
					: 0;
				if (dragDuration === 0) {
					pose.dragT = 1;
				} else {
					dragLiftTween = gsap.to(pose, {
						dragT: 1,
						duration: dragDuration,
						ease: "power2.out",
						onComplete: () => {
							dragLiftTween = null;
						}
					});
				}
			}
		}

		const draggedX = dragWorldX / handEmToWorld;
		const targetIndex = findReorderTargetIndex(slots, dragIndex, draggedX, 0.8);

		if (targetIndex !== dragIndex) {
			const displacedCardId = orderIds[targetIndex];
			if (displacedCardId !== undefined) {
				const direction = targetIndex > dragIndex ? -1 : 1;
				cardRegistry.triggerPunch(String(displacedCardId), direction * 30);
			}
			orderIds = computeReorderedIds(orderIds, dragIndex, targetIndex);
			dragIndex = targetIndex;
		}

		// Drop target: over the pile, and would a play actually be accepted
		// right now? One flag drives both the pile's highlight and the release
		// decision, so they can never disagree.
		const draggedCard = orderedCards.find((c) => c.id === gestureCardId);
		const droppable = draggedCard?.can_play !== false && !storeGame.isActionPending;
		dragOverDiscard =
			droppable &&
			isOverDiscard(
				dragWorldX,
				dragWorldZ,
				placement.discardX,
				placement.discardZ,
				discardDropRadius
			);
		bus.setDraggingOverDiscard(dragOverDiscard);
	}

	// The pointer ends a gesture wherever it happens to be, which is rarely over
	// the zone it started on and often over no zone at all — so hover is cleared
	// here rather than waiting for a pointerleave that may never arrive. That
	// missing leave event is what used to strand a dragged card in its lifted pose.
	function endGesture() {
		dragLiftTween?.kill();
		dragLiftTween = null;
		window.removeEventListener("pointermove", handleGestureMove);
		const releasedId = gestureCardId;
		const hadMoved = gestureMoved;
		const mode = gestureMode;
		// A touch long-press opened the card detail popover on this same press;
		// releasing must not also select/play the card it inspected.
		const longPressed = storeCardDetail.consumeLongPress();

		gestureCardId = null;
		gestureMode = null;
		gestureMoved = false;
		hoveredId = null;
		const wasOverDiscard = dragOverDiscard;
		dragOverDiscard = false;
		bus.setDraggingOverDiscard(false);

		if (releasedId !== null && !hadMoved) {
			const releasedCardIdStr = String(releasedId);
			const pose = cardRegistry.getPose(releasedCardIdStr);
			if (pose) pose.dragT = 0;
			draggingId = null;
			movementDelta = 0;
			dragTiltDeg = 0;
			if (longPressed) return;
			if (storeRenderSettings.clickToPlay) {
				onPlay(releasedId);
			} else {
				onSelectionChange(selectedId === releasedId ? null : releasedId);
			}
			return;
		}

		// A scroll gesture only ever moved the row; the card itself never left
		// its slot, so there is nothing to settle or play.
		if (mode === "scroll") {
			draggingId = null;
			movementDelta = 0;
			dragTiltDeg = 0;
			return;
		}

		if (releasedId !== null && hadMoved) {
			const releasedCardIdStr = String(releasedId);
			const pose = cardRegistry.getPose(releasedCardIdStr);

			if (wasOverDiscard) {
				// Committed by dropping on the pile. Mark the card in-transit so
				// this row's own layout effect doesn't tween it back to its slot
				// during the server round-trip, then hand baseBeats the pose it
				// was released at so the play flight starts there instead of
				// snapping back to the old hand slot (localCardAnchor). Ease the
				// drag lift off meanwhile. If the play is somehow refused, the
				// card stays in hand and a fresh drag settles it back.
				cardRegistry.markInTransit(releasedCardIdStr, true);
				if (pose) {
					bus.setPendingLocalDragPlay({
						id: releasedId,
						x: pose.x,
						y: pose.y,
						z: pose.z,
						spinDeg: pose.spinDeg,
						flipDeg: pose.flipDeg,
						scale: pose.scale,
						turned: pose.turned
					});
					const releaseDuration = storeAnimation.enabled
						? 0.12 / Math.max(0.1, storeAnimation.speedMultiplier)
						: 0;
					if (releaseDuration > 0) {
						gsap.to(pose, { dragT: 0, duration: releaseDuration, ease: "power2.out" });
					} else {
						pose.dragT = 0;
					}
				}
				draggingId = null;
				movementDelta = 0;
				dragTiltDeg = 0;
				onPlay(releasedId);
				return;
			}

			settleTween?.kill();
			const targetSlotX = slots[dragIndex] ? slots[dragIndex].x * handEmToWorld : 0;
			const targetSlotY = dragIndex * HAND_STACK_STEP;
			const targetSlotZ = placement.localSeatZ;

			const settleDuration = storeAnimation.enabled
				? 0.18 / Math.max(0.1, storeAnimation.speedMultiplier)
				: 0;

			if (!pose || settleDuration === 0) {
				if (pose) {
					pose.x = targetSlotX;
					pose.y = targetSlotY;
					pose.z = targetSlotZ;
					pose.dragT = 0;
				}
				draggingId = null;
				movementDelta = 0;
				dragTiltDeg = 0;
				cardRegistry.markInTransit(releasedCardIdStr, false);
				cardRegistry.applyIdlePoseIfNotInTransit(releasedCardIdStr);
			} else {
				settlingCardId = releasedCardIdStr;
				cardRegistry.markInTransit(releasedCardIdStr, true);
				draggingId = null;
				movementDelta = 0;
				dragTiltDeg = 0;
				settleTween = gsap.to(pose, {
					x: targetSlotX,
					y: targetSlotY,
					z: targetSlotZ,
					dragT: 0,
					duration: settleDuration,
					ease: "back.out(1.2)",
					onComplete: () => {
						settleTween = null;
						cardRegistry.markInTransit(releasedCardIdStr, false);
						cardRegistry.applyIdlePoseIfNotInTransit(releasedCardIdStr);
						settlingCardId = null;
					}
				});
			}
		} else {
			draggingId = null;
			movementDelta = 0;
			dragTiltDeg = 0;
		}
	}

	// On `window`, not the row itself, so scrolling works no matter where the
	// pointer is — a card game's wheel scroll shouldn't require parking the
	// mouse over a specific strip first. Re-anchoring on the clamped
	// line.scrollEm rather than accumulating keeps the offset in range.
	$effect(() => {
		if (readOnly || !pointerMode.canHover) return;
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

{#if !readOnly}
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
{/if}

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

{#if !readOnly}
	<HTML position={[sortButtonX, 1, placement.localSeatZ]} center pointerEvents="auto">
		<button
			class="sort-button pixel-corners"
			onclick={sortByRgby}
			title="Sort hand"
			aria-label="Sort hand"
		>
			<i class="hn pix hn-sort"></i>
		</button>
	</HTML>
{/if}

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
