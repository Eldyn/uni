<script lang="ts">
	import { DragDropProvider } from "@dnd-kit/svelte";
	import { isSortable } from "@dnd-kit/svelte/sortable";
	import { untrack } from "svelte"; // <-- Add untrack back

	import { storeGame, type Card } from "../../stores/game.svelte";
	import SortableCardSlot from "./SortableCardSlot.svelte";
	import { useCardBus } from "./card-bus.svelte";

	let { playableCardIds = new Set<number>() }: { playableCardIds?: Set<number> } = $props();

	const bus = useCardBus();
	let handEl = $state<HTMLElement | null>(null);
	let dragging = $state(false);

	let items = $state<Card[]>(storeGame.localPlayer?.hand ?? []);
	let snapshot = $state<Card[]>([]);

	// 1. Reactively track the true hand from the server store
	let hand = $derived(storeGame.localPlayer?.hand ?? []);

	$effect(() => {
		if (handEl) bus.register("hand-local", handEl);
		return () => bus.unregister("hand-local");
	});

	// 2. Safely sync the server hand with the local draggable items
	$effect(() => {
		const current = hand;

		// Don't interrupt if the player is actively dragging a card
		if (dragging) return;

		untrack(() => {
			const currentIds = new Set(current.map((c) => c.id));
			const incoming = current.filter((c) => !items.some((item) => item.id === c.id));
			const surviving = items.filter((c) => currentIds.has(c.id));

			// Only overwrite local state if the actual inventory of cards changed (draw/play)
			if (incoming.length > 0 || surviving.length !== items.length) {
				items = [...surviving, ...incoming];
			}
		});
	});

	function onDragStart() {
		dragging = true;
		snapshot = items.slice();
	}

	function onDragOver(event: any) {
		const { source, target } = event.operation;

		if (isSortable(source) && isSortable(target)) {
			const fromIndex = source.index;
			const toIndex = target.index;

			if (fromIndex !== toIndex) {
				const newItems = [...items];
				const [removed] = newItems.splice(fromIndex, 1);
				newItems.splice(toIndex, 0, removed);
				items = newItems;
			}
		}
	}

	function onDragEnd(event: any) {
		dragging = false;

		if (event.canceled) {
			items = snapshot;
		}
	}

	function handleCardClick(cardId: number) {
		if (!dragging) storeGame.playCard(cardId);
	}
</script>

<DragDropProvider {onDragStart} {onDragOver} {onDragEnd}>
	<div bind:this={handEl} class="player_hand" class:is-active-drag={dragging}>
		{#each items as card, index (card.id)}
			<SortableCardSlot
				{card}
				{index}
				isHidden={bus.hiddenCardIds.has(card.id)}
				isPlayable={playableCardIds.has(card.id)}
				onCardClick={handleCardClick}
			/>
		{/each}
	</div>
</DragDropProvider>

<style>
	.player_hand {
		position: absolute;
		top: 20%;
		left: 50%;
		transform: translate(-50%, -40%);

		display: flex;
		flex-direction: row;
		align-items: flex-end;

		overflow: visible;
		height: calc(var(--cardSize) * 1.5357 + 1em);
		outline: none;
	}

	.player_hand:not(.is-active-drag) :global(.card-slot:hover) {
		transform: translateY(-1em);
		z-index: 50 !important;
	}

	.player_hand :global(.card-slot + .card-slot) {
		margin-left: calc(var(--cardSize) * -0.35);
	}

	.player_hand:not(.is-active-drag) :global(.card-slot) {
		transition: transform 0.2s cubic-bezier(0.25, 1, 0.5, 1);
	}

	.player_hand:not(.is-active-drag) :global(.card-slot:hover) {
		transform: translateY(-1.5em) !important;
		z-index: 50 !important;
	}

	.player_hand:not(.is-active-drag) :global(.card-slot:hover ~ .card-slot) {
		transform: translateX(2.8em) !important;
	}
</style>
