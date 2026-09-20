<!-- Real accumulating discard pile: renders the entire client-side
     discardHistory (card-bus.svelte.ts) as a scattered stack at the playmat
     center, each card keeping its fixed seeded rotation/jitter (see
     layout/discardPile.ts), newest on top. Every card also drops a soft black
     drop shadow — without it, same-colored cards visually merge into one blob
     as they stack. No decorative base card — an empty history means an
     honestly empty pile, matching the "render ALL cards, none faked" rule the
     rest of the board follows. -->
<script lang="ts">
	import { T } from "@threlte/core";
	import {
		discardStackZ,
		discardCardOffset,
		discardStepFor,
		type DiscardEntry
	} from "../layout/discardPile";
	import { useCardRegistry } from "../animation/cardRegistry.svelte";
	import { useCardBus } from "../card-bus.svelte";
	import { loadTexture } from "./textures";
	import { CARD_WIDTH, CARD_HEIGHT } from "./units";
	import type { BoardPlacement } from "../layout/boardPlacement";
	import { storeCardDetail } from "$stores/cardDetail.svelte";

	let {
		history,
		placement,
		armed = false,
		onConfirm
	}: {
		history: DiscardEntry[];
		placement: BoardPlacement;
		/** A card in the local hand is picked and waiting to be committed here. */
		armed?: boolean;
		onConfirm?: () => void;
	} = $props();

	// Shared with discardPile.ts's previewDiscardLanding, so a card's flight
	// lands at exactly the height it's about to statically render at here.
	let scale = $derived(placement.centerScale);

	// The shadow reuses the card background texture as a silhouette (rounded
	// corners included) tinted black, the same recipe the hand uses. It sits
	// just below its own card but above the card beneath: half the pile's step
	// down still clears the previous card's top layer. Drawn through
	// CardMesh3D's own `shadow` decoration (rather than a second, separately
	// offset mesh here), so there is one shadow per card — the duplicate mesh
	// was the visible "double shadow" — and its offset stays world-fixed as the
	// card's seed rotation spins it.
	const SHADOW_OFFSET = 0.09;
	const SHADOW_OPACITY = 0.22;
	// Half the pile's CURRENT (compressed) step, not the comfortable baseline:
	// once a tall pile compresses its step below the baseline, a baseline-sized
	// drop would sink the shadow under the card beneath it.
	let shadowDropY = $derived(discardStepFor(history.length) / 2);

	// The halo hugs the top card so the pile reads as "tap this one", and the tap
	// target is a plain plane over it — big enough to hit with a thumb, and above
	// the whole stack so no individual card can swallow the raycast. An empty
	// pile still has to be tappable (the opening play lands on nothing).
	const CONFIRM_TARGET_Y = 1;
	const CONFIRM_TARGET_SCALE = 1.6;
	let topIndex = $derived(history.length - 1);

	const cardRegistry = useCardRegistry();
	const bus = useCardBus();
	// A card picked up in the hand and dragged over the pile counts as armed
	// too, so the same "tap here" pulse becomes the drop cue. LocalHand3D owns
	// whether the drop would actually be accepted (legal card, player's turn).
	// `bus` is absent when this pile is mounted standalone (tests), hence the
	// optional read.
	let dropArmed = $derived(armed || bus?.draggingOverDiscard === true);

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

	// One registry entry per discard-pile card, kept idle at this pile's own
	// jitter/rotation/stack pose whenever it isn't mid-flight — mirrors
	// LocalHand3D's own registration effect (Task A10) for the exact same
	// "no separate flight identity" reason.
	$effect(() => {
		for (const [i, entry] of history.entries()) {
			const idString = String(entry.card.id);
			const offset = discardCardOffset(placement.discardX, placement.discardZ, entry, scale);
			const pose = cardRegistry.ensureEntry(
				idString,
				{
					x: offset.x,
					y: discardStackZ(i, history.length),
					z: offset.z,
					spinDeg: entry.rotationDeg,
					flipDeg: 0,
					scale,
					turned: false,
					opacity: 1
				},
				{ type: entry.card.type, value: entry.card.value, wildColor: entry.wildColor }
			);
			cardRegistry.setPoseProvider(idString, () => {
				const p = discardCardOffset(placement.discardX, placement.discardZ, entry, scale);
				return [p.x, discardStackZ(i, history.length), p.z];
			});
			// ensureEntry is idempotent, so a pile card created before a resize
			// keeps its draw-time scale forever unless this re-syncs it — the bug
			// that left already-played cards oversized after a resolution change.
			// Mirrors LocalHand3D/PlayerSeat3D; skipped mid-flight so a flight's
			// own tween owns the scale.
			if (!cardRegistry.isInTransit(idString)) {
				pose.scale = scale;
			}
			cardRegistry.applyIdlePoseIfNotInTransit(idString);
			cardRegistry.setDecoration(idString, {
				tableBound: true,
				shadow: shadowTexture
					? {
							texture: shadowTexture,
							offsetX: SHADOW_OFFSET,
							dropZ: shadowDropY,
							opacity: SHADOW_OPACITY
						}
					: undefined,
				highlight: dropArmed && i === topIndex ? { pulse: true } : undefined
			});
		}
	});
</script>

{#if armed}
	<T.Mesh
		position.y={CONFIRM_TARGET_Y}
		rotation.x={-Math.PI / 2}
		onclick={() => {
			// A long-press on the pile inspected a card; don't also commit the play.
			if (storeCardDetail.consumeLongPress()) return;
			onConfirm?.();
		}}
	>
		<T.PlaneGeometry
			args={[CARD_WIDTH * scale * CONFIRM_TARGET_SCALE, CARD_HEIGHT * scale * CONFIRM_TARGET_SCALE]}
		/>
		<T.MeshBasicMaterial transparent opacity={0} depthWrite={false} />
	</T.Mesh>
{/if}
