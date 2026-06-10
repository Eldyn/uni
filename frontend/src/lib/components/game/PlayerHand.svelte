<script lang="ts">
	import { DragDropProvider } from "@dnd-kit-svelte/svelte";
	import { storeGame, type Card } from "../../stores/game.svelte";
	import { useCardBus } from "./card-bus.svelte";
	import GameCard from "./GameCard.svelte";

	let { playableCardIds = new Set<number>() }: { playableCardIds?: Set<number> } = $props();

	const bus = useCardBus();

	let handEl = $state<HTMLElement | null>(null);

	$effect(() => {
		if (handEl) bus.register("hand-local", handEl);
		return () => bus.unregister("hand-local");
	});

	let hand = $derived(storeGame.localPlayer?.hand ?? []);
	let items = $state<Card[]>([]);

	// Sync with game hand when not dragging
	$effect(() => {
		const current = hand;
		const currentIds = new Set(current.map((c) => c.id));
		const incoming = current.filter((c) => !items.some((item) => item.id === c.id));
		const surviving = items.filter((c) => currentIds.has(c.id));

		if (incoming.length > 0 || surviving.length !== items.length) {
			items = [...surviving, ...incoming];
		}
	});

	function handleDragEnd(event: any) {
		const { active, over } = event;

		if (!over || active.id === over.id) return;

		const activeIndex = items.findIndex((item) => item.id === active.id);
		const overIndex = items.findIndex((item) => item.id === over.id);

		if (activeIndex !== -1 && overIndex !== -1) {
			const newItems = [...items];
			[newItems[activeIndex], newItems[overIndex]] = [newItems[overIndex], newItems[activeIndex]];
			items = newItems;
		}
	}

	function handleCardClick(cardId: number) {
		storeGame.playCard(cardId);
	}
</script>

<DragDropProvider onCollision={closestCenter} onDragEnd={handleDragEnd}>
	<div bind:this={handEl} class="player_hand">
		{#each items as card, i (card.id)}
			<GameCard
				{card}
				index={i}
				isPlayable={playableCardIds.has(card.id)}
				isHidden={bus.hiddenCardIds.has(card.id)}
				onCardClick={() => handleCardClick(card.id)}
			/>
		{/each}
	</div>
</DragDropProvider>

<style>
	.player_hand {
		position: absolute;
		top: 20%;
		left: 50%;
		transform: translate(-50%, -30%);

		display: flex;
		flex-direction: row;
		align-items: flex-end;

		overflow: visible;
		height: calc(var(--cardSize) * 1.5357 + 1em);
		outline: none;
	}

	.player_hand :hover {
		z-index: 50 !important;
	}

	.player_hand :hover ~ :global(*) {
		transform: translateX(1em);
		transition: transform 150ms ease;
	}
</style>
