<!-- Keyboard/screen-reader path for playing a card and drawing. A plain DOM
     sibling of the Threlte <Canvas> (not inside it) — reads storeGame
     directly, exactly like GameHud's Exit button reads storeLobby directly.
     Every control here calls the same store methods the pointer path already
     calls, guarded by the same isActionPending the store itself enforces, so
     there's no way to double-submit a play.

     Selection and focus are shared with the pointer/3D path via props owned
     by GameBoard: `selectedId` is the same two-step pick the touch gesture
     uses (Enter picks, Enter again confirms — no separate "play" step, and
     no live-region-only feedback), and `onFocusChange` lifts a card in the
     3D hand the same way a mouse hover would, so Tab/Arrow navigation reads
     as a moving highlight on the actual card instead of an on-screen text
     strip. The buttons themselves stay screen-reader-only at all times (never
     unhidden on focus) — the 3D highlight is the sighted feedback now. -->
<script lang="ts">
	import { storeGame, type Card, type CardValue } from "$stores/game.svelte";

	const VALUE_LABELS: Partial<Record<CardValue, string>> = {
		skip: "skip",
		reverse: "reverse",
		"+2": "draw two",
		jolly: "wild",
		jolly_draw4: "wild draw four"
	};

	// How far PageUp/PageDown jump — a fixed "handful of cards" rather than a
	// measured visible-card count, since this list has no layout of its own to
	// measure (see the file doc: these buttons are permanently visually hidden).
	const PAGE_JUMP_CARDS = 5;

	let {
		selectedId = null,
		onSelectionChange,
		onPlay,
		onFocusChange
	}: {
		selectedId?: number | null;
		onSelectionChange: (cardId: number | null) => void;
		onPlay: (cardId: number) => void;
		onFocusChange: (cardId: number | null) => void;
	} = $props();

	function describeCard(card: Card): string {
		const value = VALUE_LABELS[card.value] ?? card.value;
		return card.type === "white" ? value : `${card.type} ${value}`;
	}

	let hand = $derived(storeGame.localPlayer?.hand ?? []);
	let buttonEls: (HTMLButtonElement | null)[] = $state([]);

	// Mirrors the touch gesture one step at a time: the first Enter/Space/click
	// on a card picks it (same `selectedId` the discard pile arms off of), the
	// second — now that it's already picked — confirms the play.
	function confirmOrSelect(card: Card) {
		if (card.can_play === false) return;
		if (storeGame.isActionPending) return;
		if (selectedId === card.id) {
			onPlay(card.id);
		} else {
			onSelectionChange(card.id);
		}
	}

	function focusIndex(target: number) {
		if (hand.length === 0) return;
		const clamped = Math.max(0, Math.min(hand.length - 1, target));
		buttonEls[clamped]?.focus();
	}

	function onCardKeydown(event: KeyboardEvent, card: Card, index: number) {
		switch (event.key) {
			case "Enter":
			case " ":
				event.preventDefault();
				confirmOrSelect(card);
				return;
			case "ArrowRight":
				event.preventDefault();
				focusIndex(index + 1);
				return;
			case "ArrowLeft":
				event.preventDefault();
				focusIndex(index - 1);
				return;
			case "Home":
				event.preventDefault();
				focusIndex(0);
				return;
			case "End":
				event.preventDefault();
				focusIndex(hand.length - 1);
				return;
			case "PageDown":
				event.preventDefault();
				focusIndex(index + PAGE_JUMP_CARDS);
				return;
			case "PageUp":
				event.preventDefault();
				focusIndex(index - PAGE_JUMP_CARDS);
				return;
			default:
				if (event.key >= "1" && event.key <= "9") {
					event.preventDefault();
					focusIndex(Number(event.key) - 1);
				}
		}
	}

	function drawCard() {
		if (storeGame.isActionPending) return;
		storeGame.drawCard();
	}

	let turnAnnouncement = $derived(
		storeGame.state
			? storeGame.state.current_turn === storeGame.localPlayer?.username
				? "Your turn"
				: `${storeGame.state.current_turn}'s turn`
			: ""
	);
</script>

{#if storeGame.state}
	<div class="accessible-hand-controls">
		<ul aria-label="Your hand">
			{#each hand as card, i (card.id)}
				<li>
					<button
						bind:this={buttonEls[i]}
						type="button"
						class="visually-hidden"
						tabindex={card.can_play === false ? -1 : 0}
						aria-disabled={card.can_play === false}
						aria-pressed={selectedId === card.id}
						aria-label={`${selectedId === card.id ? "Confirm" : "Play"} ${describeCard(card)}`}
						onclick={() => confirmOrSelect(card)}
						onkeydown={(event) => onCardKeydown(event, card, i)}
						onfocus={() => onFocusChange(card.id)}
						onblur={() => onFocusChange(null)}
					>
						{selectedId === card.id ? "Confirm" : "Play"}
						{describeCard(card)}
					</button>
				</li>
			{/each}
		</ul>

		<button type="button" class="visually-hidden" onclick={drawCard}> Draw card </button>

		<div class="visually-hidden" aria-live="polite">{turnAnnouncement}</div>
	</div>
{/if}

<style>
	.accessible-hand-controls ul {
		list-style: none;
		margin: 0;
		padding: 0;
	}

	/* Always screen-reader-only — never unhidden on focus. The 3D hand lifts
	   and highlights the focused card instead, so a sighted keyboard user
	   gets the same feedback a mouse hover already gives rather than a
	   floating text strip. Still in the tab order and accessibility tree
	   (not display:none), so screen readers see it exactly as before. */
	.visually-hidden {
		position: fixed;
		left: 0;
		top: 0;
		width: 1px;
		height: 1px;
		overflow: hidden;
		clip: rect(0, 0, 0, 0);
		white-space: nowrap;
	}
</style>
