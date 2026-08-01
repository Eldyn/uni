<!-- Keyboard/screen-reader path for playing a card and drawing. A plain DOM
     sibling of the Threlte <Canvas> (not inside it) — reads storeGame
     directly, exactly like GameHud's Exit button reads storeLobby directly,
     so it needs no coupling to Scene3D/LocalHand3D's pointer-driven gesture
     state (selectedCardId, hoveredId, drag/tap). Every control here calls the
     same store methods the pointer path already calls, guarded by the same
     isActionPending the store itself enforces, so there's no way to
     double-submit a play.

     Visually hidden until a control inside it is focused (never
     display:none, which would remove it from the tab order entirely), so
     mouse/touch play is completely unaffected — this is purely an
     alternate input path. -->
<script lang="ts">
	import { storeGame, type Card, type CardValue } from "$stores/game.svelte";

	const VALUE_LABELS: Partial<Record<CardValue, string>> = {
		skip: "skip",
		reverse: "reverse",
		"+2": "draw two",
		jolly: "wild",
		jolly_draw4: "wild draw four"
	};

	function describeCard(card: Card): string {
		const value = VALUE_LABELS[card.value] ?? card.value;
		return card.type === "white" ? value : `${card.type} ${value}`;
	}

	let hand = $derived(storeGame.localPlayer?.hand ?? []);

	function activate(card: Card) {
		if (card.can_play === false) return;
		if (storeGame.isActionPending) return;
		storeGame.playCard(card.id);
	}

	function onCardKeydown(event: KeyboardEvent, card: Card) {
		if (event.key !== "Enter" && event.key !== " ") return;
		event.preventDefault();
		activate(card);
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
			{#each hand as card (card.id)}
				<li>
					<button
						type="button"
						class="visually-hidden"
						tabindex={card.can_play === false ? -1 : 0}
						aria-disabled={card.can_play === false}
						aria-label={`Play ${describeCard(card)}`}
						onclick={() => activate(card)}
						onkeydown={(event) => onCardKeydown(event, card)}
					>
						Play {describeCard(card)}
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

	/* Visually hidden but focusable/announced — NOT display:none, which would
	   pull these out of the tab order (and, for the live region, out of the
	   accessibility tree) entirely. Standard "sr-only, visible on focus" idiom:
	   clipped to a single point until focused, then it opens into a small
	   readable strip so a keyboard user can see what they just tabbed to. */
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

	button.visually-hidden:focus {
		width: auto;
		height: auto;
		clip: auto;
		overflow: visible;
		z-index: 10001;
		padding: 0.5em 0.8em;
		background: var(--surface-deep);
		color: var(--table-text);
		border: 2px solid var(--accent);
		white-space: normal;
	}
</style>
