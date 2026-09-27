<script lang="ts">
	import { storeGame } from "$stores/game.svelte";
	import { storeI18n } from "$stores/i18n.svelte";
	import * as m from "$lib/paraglide/messages.js";
	import { formatTurnTimer } from "$utils/timeFormat";

	// All players see the countdown chip; only the responders may
	// answer it, and spectators never may.
	let activeWindow = $derived(storeGame.activeWindow);
</script>

{#if activeWindow}
	<div
		class="window-chip pixel-corners"
		role="timer"
		aria-label={m.game_window_time_left(
			{ seconds: storeGame.windowTimeRemaining },
			{ locale: storeI18n.locale }
		)}
	>
		<span class="window-time" class:urgent={storeGame.windowTimeRemaining <= 5}>
			{formatTurnTimer(storeGame.windowTimeRemaining)}
		</span>
		{#if storeGame.isWindowResponder}
			<button
				type="button"
				class="btn pixel-corners pass-btn"
				disabled={storeGame.isActionPending}
				onclick={() => storeGame.passWindow()}
			>
				{m.game_window_pass({}, { locale: storeI18n.locale })}
			</button>
		{/if}
	</div>
{/if}

<style>
	.window-chip {
		display: flex;
		align-items: center;
		gap: 12px;
		padding: 6px 14px;
		background: var(--surface-2);
		color: var(--table-text);
		font-weight: bold;
		font-size: 1.2rem;
		box-shadow: 0 0 0 4px var(--accent);
		animation: window-pop 0.2s ease-out;
	}

	.window-time {
		padding: 4px 10px;
		font-family: monospace;
		transition: color 0.3s ease;
	}

	.window-time.urgent {
		color: var(--danger);
	}

	.pass-btn {
		padding: 6px 14px;
		font-weight: bold;
		font-size: 0.9rem;
	}

	@keyframes window-pop {
		from {
			transform: scale(0.9);
			opacity: 0;
		}
		to {
			transform: scale(1);
			opacity: 1;
		}
	}

	@media (prefers-reduced-motion: reduce) {
		.window-chip {
			animation: none;
		}
	}
</style>
