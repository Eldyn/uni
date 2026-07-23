<!-- The discard pile / playmat, staying centered on the table. Unlike a single
     swapped top card, this renders the whole client-accumulated discard history
     (card-bus.svelte.ts) as a real stack of dropped cards: each one keeps a
     fixed random rotation + jitter (seeded by card id), newest on top. The very
     top card gets a pixel-shadow (a flat-silhouette duplicate offset behind it,
     no separate art) and the landing animation. -->
<script lang="ts">
	import GameCard from "./GameCard.svelte";
	import { useCardBus } from "./card-bus.svelte";

	let { gameColor = "green" }: { gameColor?: string } = $props();

	const bus = useCardBus();

	let discardPileEl = $state<HTMLElement | null>(null);

	$effect(() => {
		if (discardPileEl) bus.register("discard-pile", discardPileEl);
		return () => bus.unregister("discard-pile");
	});

	// The full scattered pile; the last entry is the current top card. Empty
	// until the first card lands, so fall back to a face-down placeholder.
	let history = $derived(bus.discardHistory);
	let topIndex = $derived(history.length - 1);
</script>

<div id="discard_pile" bind:this={discardPileEl}>
	<GameCard
		card={{ id: -1, type: "wild", value: "0" }}
		turned={true}
		extraClass="pile"
		style="left: 0;"
	/>

	{#each history as entry, i (entry.card.id + ":" + i)}
		{@const isTop = i === topIndex}
		{#if isTop}
			{#key entry.card.id}
				<div
					class="pixel-shadow-layer"
					style="transform: translate({entry.jitter[0]}em, {entry.jitter[1]}em) rotate({entry.rotationDeg}deg) translate(0.35em, 0.35em);"
				>
					<GameCard card={entry.card} extraClass="shadow-copy" style="left: 0;" />
				</div>
				<div
					class="scatter-card top-card"
					style="transform: translate({entry.jitter[0]}em, {entry.jitter[1]}em) rotate({entry.rotationDeg}deg); z-index: {100 + i};"
				>
					<GameCard card={entry.card} style="left: 0;" />
				</div>
			{/key}
		{:else}
			<div
				class="scatter-card"
				style="transform: translate({entry.jitter[0]}em, {entry.jitter[1]}em) rotate({entry.rotationDeg}deg); z-index: {50 + i};"
			>
				<GameCard card={entry.card} style="left: 0;" />
			</div>
		{/if}
	{/each}
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

	#discard_pile :global(.card.pile) {
		position: absolute;
		box-shadow:
			0px 2px white,
			0px 4px var(--shadowColor),
			0px 6px white,
			0px 8px var(--shadowColor);
	}

	.scatter-card {
		position: absolute;
		inset: 0;
		will-change: transform;
	}

	.scatter-card.top-card {
		animation: discard-land 0.3s cubic-bezier(0.22, 1, 0.36, 1) both;
	}

	.pixel-shadow-layer {
		position: absolute;
		inset: 0;
		z-index: 90;
	}

	#discard_pile :global(.card.shadow-copy) {
		filter: brightness(0) opacity(0.35);
		pointer-events: none;
	}

	/* The land animation folds in on top of each card's own fixed rotation, so
	   it only scales/settles rather than fighting the scatter transform. */
	@keyframes discard-land {
		from {
			opacity: 0.6;
			filter: brightness(1.6);
		}
		to {
			opacity: 1;
			filter: brightness(1);
		}
	}
</style>
