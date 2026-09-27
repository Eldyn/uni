<script lang="ts">
	import { storeGame } from "$stores/game.svelte";
	import { storeI18n } from "$stores/i18n.svelte";
	import * as m from "$lib/paraglide/messages.js";
	import { formatTurnTimer } from "$utils/timeFormat";

	// A prompt runs its own clock, separate from the paused turn clock.
	let visible = $derived(
		!storeGame.isSpectator && storeGame.activePrompt !== null && storeGame.promptTimeRemaining > 0
	);
</script>

{#if visible}
	<div
		class="prompt-chip pixel-corners"
		role="timer"
		aria-label={m.game_window_time_left(
			{ seconds: storeGame.promptTimeRemaining },
			{ locale: storeI18n.locale }
		)}
	>
		<span class="prompt-time" class:urgent={storeGame.promptTimeRemaining <= 5}>
			{formatTurnTimer(storeGame.promptTimeRemaining)}
		</span>
	</div>
{/if}

<style>
	.prompt-chip {
		display: flex;
		align-items: center;
		padding: 6px 14px;
		background: var(--surface-2);
		color: var(--table-text);
		font-weight: bold;
		font-size: 1.2rem;
		box-shadow: 0 0 0 4px var(--accent);
	}

	.prompt-time {
		padding: 4px 10px;
		font-family: monospace;
		transition: color 0.3s ease;
	}

	.prompt-time.urgent {
		color: var(--danger);
	}
</style>
