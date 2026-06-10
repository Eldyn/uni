<script lang="ts">
	import { storeGame } from "../../stores/game.svelte";
	import { untrack } from "svelte";
	import GameCard from "./GameCard.svelte";

	let {
		hand = [],
		hiddenCardIds = new Set<number>(),
		playableCardIds = new Set<number>(),
		handEl = $bindable<HTMLElement | null>(null)
	} = $props();

	// ─────────────────────────────────────────────────────────────────────────
	// CARD ORDER & DRAG STATE
	// ─────────────────────────────────────────────────────────────────────────

	let cardOrder = $state<Map<number, number>>(new Map());
	let draggedCardId = $state<number | null>(null);
	let dragOverCardId = $state<number | null>(null);
	let isDragging = $state(false);

	$effect(() => {
		const currentHand = hand;
		const currentIds = new Set(currentHand.map((c) => c.id));

		untrack(() => {
			const next = new Map(cardOrder);
			let mapChanged = false;

			for (const [id] of next) {
				if (!currentIds.has(id)) {
					next.delete(id);
					mapChanged = true;
				}
			}

			let maxOrd = next.size > 0 ? Math.max(...next.values()) : -1;

			for (const c of currentHand) {
				if (!next.has(c.id)) {
					next.set(c.id, ++maxOrd);
					mapChanged = true;
				}
			}

			if (
				mapChanged ||
				next.size !== cardOrder.size ||
				[...next].some(([k, v]) => cardOrder.get(k) !== v)
			) {
				cardOrder = next;
			}
		});
	});

	let sortedHand = $derived(
		[...hand].sort((a, b) => (cardOrder.get(a.id) ?? 0) - (cardOrder.get(b.id) ?? 0))
	);

	// ─────────────────────────────────────────────────────────────────────────
	// DRAG E DROP
	// ─────────────────────────────────────────────────────────────────────────

	function onDragStart(e: DragEvent, cardId: number) {
		draggedCardId = cardId;
		isDragging = true;
		e.dataTransfer?.setData("text/plain", String(cardId));
	}

	function onDragOver(e: DragEvent, cardId: number) {
		e.preventDefault();
		if (draggedCardId !== cardId) dragOverCardId = cardId;
	}

	function onDragLeave() {
		dragOverCardId = null;
	}

	function onDrop(e: DragEvent, targetId: number) {
		e.preventDefault();
		if (draggedCardId === null || draggedCardId === targetId) {
			resetDrag();
			return;
		}

		const from = cardOrder.get(draggedCardId) ?? 0;
		const to = cardOrder.get(targetId) ?? 0;
		const next = new Map(cardOrder);

		if (from < to) {
			for (const [id, o] of next) {
				if (o > from && o <= to) next.set(id, o - 1);
			}
		} else {
			for (const [id, o] of next) {
				if (o < from && o >= to) next.set(id, o + 1);
			}
		}

		next.set(draggedCardId, to);
		cardOrder = next;
		resetDrag();
	}

	function onDragEnd() {
		resetDrag();
	}

	function resetDrag() {
		draggedCardId = null;
		dragOverCardId = null;
		isDragging = false;
	}

	// ─────────────────────────────────────────────────────────────────────────
	// TOUCH REORDER
	// ─────────────────────────────────────────────────────────────────────────

	let touchStartX = $state(0);
	let touchCardId = $state<number | null>(null);
	let touchStartIdx = $state(0);

	function onTouchStart(e: TouchEvent, cardId: number) {
		touchCardId = cardId;
		touchStartX = e.touches[0].clientX;
		touchStartIdx = cardOrder.get(cardId) ?? 0;
	}

	function onTouchEnd(e: TouchEvent) {
		if (touchCardId === null) return;

		const delta = Math.round((e.changedTouches[0].clientX - touchStartX) / 50);

		if (delta !== 0) {
			const newOrd = Math.max(0, Math.min(hand.length - 1, touchStartIdx + delta));
			const target = [...cardOrder].find(([, o]) => o === newOrd);
			if (target) onDrop({ preventDefault: () => {} } as DragEvent, target[0]);
		}

		touchCardId = null;
	}

	// ─────────────────────────────────────────────────────────────────────────
	// ACTIONS
	// ─────────────────────────────────────────────────────────────────────────

	function handleCardClick(cardId: number) {
		if (!isDragging) storeGame.playCard(cardId);
	}
</script>

<div
	bind:this={handEl}
	class="player_hand"
	style="
		position: absolute;
		top: 20%; left: 50%;
		transform: translate(-50%, -30%);
		width: calc({sortedHand.length} * 2.2em + 7.2em);
		height: calc(var(--cardSize) * 1.5357);
	"
>
	{#each sortedHand as card, i (card.id)}
		<GameCard
			{card}
			index={i}
			isHidden={hiddenCardIds.has(card.id)}
			isDragged={draggedCardId === card.id}
			isDragTarget={dragOverCardId === card.id}
			isPlayable={playableCardIds.has(card.id)}
			onCardClick={handleCardClick}
			{onDragStart}
			{onDragOver}
			{onDragLeave}
			{onDrop}
			{onDragEnd}
			{onTouchStart}
			{onTouchEnd}
		/>
	{/each}
</div>

<style>
	.player_hand {
		position: relative;
	}
</style>
