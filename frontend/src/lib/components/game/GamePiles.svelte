<script lang="ts">
	import { storeGame } from "../../stores/game.svelte";
	import GameCard from "./GameCard.svelte";

	let {
		gameColor = "green",
		drawPileEl = $bindable(null),
		discardPileEl = $bindable(null)
	}: {
		gameColor?: string;
		drawPileEl: HTMLElement | null;
		discardPileEl: HTMLElement | null;
	} = $props();

	function handleDrawClick() {
		if (storeGame.state?.current_turn === storeGame.localPlayer?.username) {
			storeGame.drawCard();
		}
	}
</script>

<div id="piles_area">
	<div id="draw_pile" bind:this={drawPileEl} onclick={handleDrawClick} role="button" tabindex="0">
		<GameCard
			card={{ id: -1, color: "black", value: "" }}
			turned={true}
			extraClass="top-card"
			style="left: 0;"
		/>
		<GameCard
			card={{ id: -1, color: "black", value: "" }}
			turned={true}
			extraClass="pile"
			style="left: 0;"
		/>
	</div>

	<div id="discard_pile" bind:this={discardPileEl}>
		{#if storeGame.state?.top_card}
			{#key storeGame.state.top_card.id}
				<GameCard card={storeGame.state.top_card} extraClass="top-card" style="left: 0;" />
			{/key}
		{:else}
			<GameCard
				card={{ id: -1, color: gameColor, value: "" }}
				extraClass="top-card"
				style="left: 0;"
			/>
		{/if}
		<GameCard
			card={{ id: -1, color: "black", value: "" }}
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

	/* DRAW PILE */
	#draw_pile {
		position: absolute;
		left: 5em;
		top: 7em;
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

	/* DISCARD PILE */
	#discard_pile {
		position: absolute;
		left: 12em;
		top: 7.4em;
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

