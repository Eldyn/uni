<!-- The local player's own hand, rendered top-down in the WebGL scene like
     everything else on the board (no more DOM/HTML hand). Uses the existing
     flat fan geometry (layout/handFan.ts, previously unused) anchored at the
     local seat, south of the ring. Card order isn't meaningful to the rules,
     so unlike the old DOM PlayerHand.svelte this drops drag-to-reorder and
     keeps only click-to-play — a deliberate scope cut, not an oversight. -->
<script lang="ts">
	import { storeGame } from "$stores/game.svelte";
	import { computeHandFanSlots } from "../layout/handFan";
	import { useCardBus } from "../card-bus.svelte";
	import CardMesh3D from "./CardMesh3D.svelte";
	import { EM_TO_WORLD, LOCAL_SEAT_Z } from "./units";

	const bus = useCardBus();

	// Tiny per-card lift so the fan's overlapping cards never z-fight.
	const STACK_STEP = 0.002;

	let cards = $derived(
		(storeGame.localPlayer?.hand ?? []).filter((card) => !bus.hiddenCardIds.has(card.id))
	);
	let slots = $derived(computeHandFanSlots(cards.length));

	function handlePlay(cardId: number) {
		if (!storeGame.isActionPending) storeGame.playCard(cardId);
	}
</script>

{#each cards as card, i (card.id)}
	{@const slot = slots[i]}
	<CardMesh3D
		{card}
		position={[slot.x * EM_TO_WORLD, i * STACK_STEP, LOCAL_SEAT_Z + slot.y * EM_TO_WORLD]}
		spinDeg={slot.rotateDeg}
		onclick={() => handlePlay(card.id)}
	/>
{/each}
