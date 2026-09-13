<!-- Real accumulating discard pile: renders the entire client-side
     discardHistory (card-bus.svelte.ts) as a scattered stack at the playmat
     center, each card keeping its fixed seeded rotation/jitter (see
     layout/discardPile.ts), newest on top. Every card also drops a soft black
     shadow offset toward the bottom-right — without it, same-colored cards
     visually merge into one blob as they stack. No decorative base card — an
     empty history means an honestly empty pile, matching the "render ALL
     cards, none faked" rule the rest of the board follows. -->
<script lang="ts">
	import { T } from "@threlte/core";
	import { DISCARD_STACK_STEP, type DiscardEntry } from "../layout/discardPile";
	import CardMesh3D from "./CardMesh3D.svelte";
	import { loadTexture } from "./textures";
	import { CARD_WIDTH, CARD_HEIGHT, EM_TO_WORLD } from "./units";
	import type { BoardPlacement } from "../layout/boardPlacement";

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
	const STACK_STEP = DISCARD_STACK_STEP;
	let scale = $derived(placement.centerScale);

	// The shadow reuses the card background texture as a silhouette (rounded
	// corners included) tinted black. It sits just below its own card but above
	// the card beneath: a card's layers span ~0.006 units upward, so half the
	// stack step down still clears the previous card's top layer. Kept short and
	// faint on purpose — a long, dark offset makes the pile read as a tall tower
	// rather than a few loose cards lying on the mat.
	const SHADOW_OFFSET = 0.09;
	const SHADOW_DROP_Y = STACK_STEP / 2;
	const SHADOW_OPACITY = 0.22;

	// The halo hugs the top card so the pile reads as "tap this one", and the tap
	// target is a plain plane over it — big enough to hit with a thumb, and above
	// the whole stack so no individual card can swallow the raycast. An empty
	// pile still has to be tappable (the opening play lands on nothing).
	const CONFIRM_TARGET_Y = 1;
	const CONFIRM_TARGET_SCALE = 1.6;
	let topIndex = $derived(history.length - 1);

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

{#each history as entry, i (entry.seq)}
	{#if shadowTexture}
		<T.Mesh
			position={[
				entry.jitter[0] * EM_TO_WORLD + SHADOW_OFFSET,
				i * STACK_STEP - SHADOW_DROP_Y,
				entry.jitter[1] * EM_TO_WORLD + SHADOW_OFFSET
			]}
			rotation.x={-Math.PI / 2}
			rotation.z={(entry.rotationDeg * Math.PI) / 180}
			{scale}
		>
			<T.PlaneGeometry args={[CARD_WIDTH, CARD_HEIGHT]} />
			<T.MeshBasicMaterial
				map={shadowTexture}
				color="#000000"
				transparent
				opacity={SHADOW_OPACITY}
				depthWrite={false}
				toneMapped={false}
			/>
		</T.Mesh>
	{/if}
	<CardMesh3D
		card={entry.card}
		wildColor={entry.wildColor}
		position={[entry.jitter[0] * EM_TO_WORLD, i * STACK_STEP, entry.jitter[1] * EM_TO_WORLD]}
		spinDeg={entry.rotationDeg}
		{scale}
		highlight={armed && i === topIndex ? { pulse: true } : undefined}
	/>
{/each}

{#if armed}
	<T.Mesh position.y={CONFIRM_TARGET_Y} rotation.x={-Math.PI / 2} onclick={() => onConfirm?.()}>
		<T.PlaneGeometry
			args={[CARD_WIDTH * scale * CONFIRM_TARGET_SCALE, CARD_HEIGHT * scale * CONFIRM_TARGET_SCALE]}
		/>
		<T.MeshBasicMaterial transparent opacity={0} depthWrite={false} />
	</T.Mesh>
{/if}
