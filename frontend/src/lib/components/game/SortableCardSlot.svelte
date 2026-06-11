<script lang="ts">
	import { createSortable } from "@dnd-kit/svelte/sortable";
	import GameCard from "./GameCard.svelte";
	import type { Card } from "../../stores/game.svelte";

	let {
		card,
		index,
		isHidden,
		isPlayable,
		onCardClick
	}: {
		card: Card;
		index: number;
		isHidden: boolean;
		isPlayable: boolean;
		onCardClick: (id: number) => void;
	} = $props();

	const sortable = createSortable({
		get id() {
			return card.id;
		},
		get index() {
			return index;
		}
	});
</script>

<div class="card-slot" class:is-dragging={sortable.isDragging} {@attach sortable.attach}>
	<GameCard {card} {isHidden} {isPlayable} onCardClick={() => onCardClick(card.id)} />
</div>

<style>
	.card-slot {
		/* True physical size so the library can calculate collisions properly */
		width: var(--cardSize);
		height: calc(var(--cardSize) * 1.5357);
		flex-shrink: 0;
		position: relative;
		cursor: grab;
		/* Do not put transform transitions here; the library handles them */
	}

	.card-slot + .card-slot {
		margin-left: calc(var(--cardSize) * -0.5); /* Negative margin for fan overlap */
	}

	.card-slot:active {
		cursor: grabbing;
	}

	.card-slot.is-dragging {
		opacity: 0.8;
		filter: drop-shadow(0 10px 15px rgba(0, 0, 0, 0.5));
	}
</style>
