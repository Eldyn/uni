<!-- The discard pile / playmat, staying centered on the table (renamed from
     GamePiles.svelte now that the draw pile has moved out to sit beside the
     local hand instead). The top card gets a pixel-shadow: a flat-silhouette
     duplicate of the same sprite offset behind it, no separate art asset. -->
<script lang="ts">
	import { storeGame } from "$stores/game.svelte";
	import GameCard from "./GameCard.svelte";
	import { useCardBus } from "./card-bus.svelte";

	let { gameColor = "green" }: { gameColor?: string } = $props();

	const bus = useCardBus();

	let discardPileEl = $state<HTMLElement | null>(null);

	$effect(() => {
		if (discardPileEl) bus.register("discard-pile", discardPileEl);
		return () => bus.unregister("discard-pile");
	});

	let topCard = $derived(bus.discardTop ?? { id: -1, type: gameColor, value: "0" as const });
</script>

<div id="discard_pile" bind:this={discardPileEl}>
	{#key topCard.id}
		<div class="pixel-shadow-layer">
			<GameCard card={topCard} extraClass="shadow-copy" style="left: 0;" />
		</div>
		<GameCard card={topCard} extraClass="top-card" style="left: 0;" />
	{/key}
	<GameCard
		card={{ id: -1, type: "wild", value: "0" }}
		turned={true}
		extraClass="pile"
		style="left: 0;"
	/>
</div>

<style>
	#discard_pile {
		position: absolute;
		left: 50%;
		top: 50%;
		width: var(--cardSize);
		height: calc(var(--cardSize) * 1.5357);
		transform: translate(-50%, -50%);
	}

	#discard_pile :global(.card.top-card),
	#discard_pile :global(.card.pile),
	#discard_pile :global(.card.shadow-copy) {
		position: absolute;
	}

	#discard_pile :global(.card.pile) {
		box-shadow:
			0px 2px white,
			0px 4px var(--shadowColor),
			0px 6px white,
			0px 8px var(--shadowColor);
	}

	.pixel-shadow-layer {
		position: absolute;
		inset: 0;
		transform: translate(0.35em, 0.35em);
		z-index: 90;
	}

	#discard_pile :global(.card.shadow-copy) {
		filter: brightness(0) opacity(0.35);
		pointer-events: none;
	}

	#discard_pile :global(.card.top-card) {
		z-index: 100;
		box-shadow: none;
		animation: discard-land 0.3s cubic-bezier(0.22, 1, 0.36, 1) both;
	}

	@keyframes discard-land {
		from {
			transform: scale(1.15) rotate(4deg);
			opacity: 0.6;
		}
		to {
			transform: scale(1) rotate(0deg);
			opacity: 1;
		}
	}
</style>
