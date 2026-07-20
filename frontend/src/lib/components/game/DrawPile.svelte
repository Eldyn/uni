<!-- The draw pile, extracted out of the old GamePiles.svelte so it can sit
     beside the local player's hand instead of centered on the playmat
     (the discard pile, now DiscardPile.svelte, keeps the center spot). -->
<script lang="ts">
	import { storeGame } from "$stores/game.svelte";
	import GameCard from "./GameCard.svelte";
	import { useCardBus } from "./card-bus.svelte";

	const bus = useCardBus();

	let drawPileEl = $state<HTMLElement | null>(null);

	$effect(() => {
		if (drawPileEl) bus.register("draw-pile", drawPileEl);
		return () => bus.unregister("draw-pile");
	});

	function handleDrawClick() {
		if (storeGame.state?.current_turn === storeGame.localPlayer?.username) {
			storeGame.drawCard();
		}
	}
</script>

<div
	id="draw_pile"
	bind:this={drawPileEl}
	onclick={handleDrawClick}
	onkeydown={(e) => (e.key === "Enter" || e.key === " ") && handleDrawClick()}
	role="button"
	tabindex="0"
>
	<GameCard
		card={{ id: -1, type: "wild", value: "0" }}
		turned={true}
		extraClass="top-card"
		style="left: 0;"
	/>
	<GameCard
		card={{ id: -1, type: "wild", value: "0" }}
		turned={true}
		extraClass="pile"
		style="left: 0;"
	/>
</div>

<style>
	#draw_pile {
		position: relative;
		width: var(--cardSize);
		height: calc(var(--cardSize) * 1.5357);
	}

	#draw_pile :global(.card.top-card),
	#draw_pile :global(.card.pile) {
		position: absolute;
	}

	#draw_pile :global(.card.pile) {
		box-shadow:
			0px 2px white,
			0px 4px var(--shadowColor),
			0px 6px white,
			0px 8px var(--shadowColor),
			0px 10px white,
			0px 12px var(--shadowColor),
			0px 14px white,
			0px 16px var(--shadowColor),
			0px 18px white,
			0px 20px var(--shadowColor);
	}

	#draw_pile :global(.card.top-card) {
		z-index: 100;
		box-shadow: none;
		transition:
			transform 200ms ease,
			box-shadow 200ms ease;
		cursor: pointer;
	}

	#draw_pile :global(.card.top-card:hover) {
		box-shadow: 0px 4px var(--shadowColor);
		transform: translateY(1em);
	}
</style>
