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
	import type { DiscardEntry } from "../layout/discardPile";
	import CardMesh3D from "./CardMesh3D.svelte";
	import CardHighlight3D from "./CardHighlight3D.svelte";
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

	// CardMesh3D's own layered planes sit up to 0.004 world units apart; a
	// per-card step smaller than that lets one card's layers interleave with
	// its neighbor's (z-fighting) — 0.02 clears that with margin.
	const STACK_STEP = 0.02;
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
	// The confirm halo also sits below the top card, but half a step down lands
	// it exactly on the shadow's own plane — two transparent silhouettes fighting
	// for the same depth. Splitting the difference clears both the shadow above
	// and the previous card's top layer below.
	const HALO_DROP_Y = SHADOW_DROP_Y + STACK_STEP / 4;

	// The halo hugs the top card so the pile reads as "tap this one", and the tap
	// target is a plain plane over it — big enough to hit with a thumb, and above
	// the whole stack so no individual card can swallow the raycast. An empty
	// pile still has to be tappable (the opening play lands on nothing).
	const CONFIRM_TARGET_Y = 1;
	const CONFIRM_TARGET_SCALE = 1.6;
	let topEntry = $derived(history[history.length - 1]);
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
	/>
{/each}

{#if armed}
	<CardHighlight3D
		position={[
			(topEntry?.jitter[0] ?? 0) * EM_TO_WORLD,
			Math.max(0, topIndex * STACK_STEP - HALO_DROP_Y),
			(topEntry?.jitter[1] ?? 0) * EM_TO_WORLD
		]}
		spinDeg={topEntry?.rotationDeg ?? 0}
		{scale}
		pulse
	/>
	<T.Mesh position.y={CONFIRM_TARGET_Y} rotation.x={-Math.PI / 2} onclick={() => onConfirm?.()}>
		<T.PlaneGeometry
			args={[CARD_WIDTH * scale * CONFIRM_TARGET_SCALE, CARD_HEIGHT * scale * CONFIRM_TARGET_SCALE]}
		/>
		<T.MeshBasicMaterial transparent opacity={0} depthWrite={false} />
	</T.Mesh>
{/if}
