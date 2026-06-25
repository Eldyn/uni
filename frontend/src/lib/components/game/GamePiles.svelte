<script lang="ts">
	import { storeGame } from "../../stores/game.svelte";
	import GameCard from "./GameCard.svelte";
	import { useCardBus } from "./card-bus.svelte";

	let { gameColor = "green" }: { gameColor?: string } = $props();

	const bus = useCardBus();

	// Register the *stable* pile wrappers (sized to a card in CSS below) rather
	// than the top card itself: the discard top card is wrapped in {#key} and so
	// is destroyed/recreated on every play, which would race the overlay's flight
	// effect and fall back to screen centre. The wrappers persist for the match.
	let drawPileEl = $state<HTMLElement | null>(null);
	let discardPileEl = $state<HTMLElement | null>(null);

	$effect(() => {
		if (drawPileEl) bus.register("draw-pile", drawPileEl);
		return () => bus.unregister("draw-pile");
	});

	$effect(() => {
		if (discardPileEl) bus.register("discard-pile", discardPileEl);
		return () => bus.unregister("discard-pile");
	});

	function handleDrawClick() {
		if (storeGame.state?.current_turn === storeGame.localPlayer?.username) {
			storeGame.drawCard();
		}
	}
</script>

<div id="piles_area">
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

	<div id="discard_pile" bind:this={discardPileEl}>
		{#if bus.discardTop}
			{#key bus.discardTop.id}
				<GameCard card={bus.discardTop} extraClass="top-card" style="left: 0;" />
			{/key}
		{:else}
			<GameCard
				card={{ id: -1, type: gameColor, value: "0" }}
				extraClass="top-card"
				style="left: 0;"
			/>
		{/if}
		<GameCard
			card={{ id: -1, type: "wild", value: "0" }}
			turned={true}
			extraClass="pile"
			style="left: 0;"
		/>
	</div>
</div>

<style>
	:root {
		--shadowColor: rgba(0, 0, 0, 0.16);
	}

	#piles_area {
		position: relative;
		border-radius: 4em;
		transition: background-color 300ms;
		grid-area: 2 / 2;
		transform: translateY(-2.8em);
		width: 100%;
		height: 100%;
	}

	#draw_pile {
		position: absolute;
		left: 5em;
		top: 7em;
		/* Size to a card so the flight bus measures the card's centre, not a
		   collapsed 0×0 corner (children below are absolutely positioned). */
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

	#discard_pile {
		position: absolute;
		left: 12em;
		top: 7.4em;
		/* Size to a card so the flight bus measures the card's centre, not a
		   collapsed 0×0 corner (children below are absolutely positioned). */
		width: var(--cardSize);
		height: calc(var(--cardSize) * 1.5357);
	}

	#discard_pile :global(.card.top-card),
	#discard_pile :global(.card.pile) {
		position: absolute;
	}

	#discard_pile :global(.card.pile) {
		box-shadow:
			0px 2px white,
			0px 4px var(--shadowColor),
			0px 6px white,
			0px 8px var(--shadowColor);
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
