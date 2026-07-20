<script lang="ts">
	import { storeGame } from "$stores/game.svelte";
	import { computeTurnOrderWindow } from "$utils/turnOrder";

	let turnWindow = $derived(
		storeGame.state
			? computeTurnOrderWindow(
					storeGame.state.players,
					storeGame.state.current_turn,
					storeGame.state.play_direction
				)
			: { prev: [], current: null, next: [] }
	);

	// prev is nearest-first-from-current; the strip reads chronologically
	// left-to-right, so the display order is reversed.
	let displayPrev = $derived([...turnWindow.prev].reverse());
</script>

{#if turnWindow.current}
	<div class="turn-order-strip" aria-label="Turn order">
		{#each displayPrev as p (p.username)}
			<span class="chip dim" title={p.username}>{p.username}</span>
		{/each}
		<span class="chip current" title={turnWindow.current.username}>
			{turnWindow.current.username}
		</span>
		{#each turnWindow.next as p (p.username)}
			<span class="chip dim" title={p.username}>{p.username}</span>
		{/each}
	</div>
{/if}

<style>
	.turn-order-strip {
		display: flex;
		align-items: center;
		gap: 0.35em;
		font-size: 0.7rem;
		font-weight: bold;
	}

	.chip {
		max-width: 6em;
		overflow: hidden;
		text-overflow: ellipsis;
		white-space: nowrap;
		padding: 2px 8px;
		border-radius: 4px;
		background: var(--surface-2);
		color: white;
	}

	.chip.current {
		background: var(--accent);
	}

	.chip.dim {
		opacity: 0.55;
	}
</style>
