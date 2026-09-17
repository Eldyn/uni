<script lang="ts">
	import { storeGame } from "$stores/game.svelte";
	import { storeLobby } from "$stores/lobby.svelte";
	import TurnOrderStrip from "./TurnOrderStrip.svelte";
	import { storeI18n } from "$stores/i18n.svelte";
	import * as m from "$lib/paraglide/messages.js";

	let collapsed = $state(false);

	function handleReturnToLobbies() {
		storeLobby.leave();
	}
</script>

{#if storeGame.state && !storeGame.actionRequired}
	<div class="hud-container" class:collapsed>
		<button
			class="collapse-toggle"
			onclick={() => (collapsed = !collapsed)}
			aria-expanded={!collapsed}
			aria-label={collapsed
				? m.game_hud_expand({}, { locale: storeI18n.locale })
				: m.game_hud_collapse({}, { locale: storeI18n.locale })}
		>
			<i class="hn pix {collapsed ? 'hn-angle-small-down' : 'hn-angle-small-up'}"></i>
		</button>

		{#if !collapsed}
			{#if storeGame.isSpectator}
				<span class="spectating-badge pixel-corners">
					{m.game_spectating_indicator({}, { locale: storeI18n.locale })}
				</span>
			{/if}

			{#if storeGame.spectatorCount > 0}
				<span
					class="spectator-counter pixel-corners"
					title={m.game_spectator_count({ count: storeGame.spectatorCount }, { locale: storeI18n.locale })}
				>
					<i class="hn hn-eye pix"></i> {storeGame.spectatorCount}
				</span>
			{/if}

			<span
				class="timer pixel-corners"
				style="background: {storeGame.turnTimeRemaining <= 5
					? 'var(--danger)'
					: 'var(--surface-2)'};"
			>
				00:{storeGame.turnTimeRemaining.toString().padStart(2, "0")}
			</span>

			<TurnOrderStrip />

			{#if storeGame.state?.mode === 'elimination' && storeGame.placements.length > 0}
				<div
					class="elimination-standings pixel-corners"
					title={m.game_placement_standings({}, { locale: storeI18n.locale })}
				>
					{#each storeGame.placements as name, i}
						<span class="placement-chip">
							{m.game_placement_rank({ rank: i + 1, name }, { locale: storeI18n.locale })}
						</span>
					{/each}
				</div>
			{/if}

			<button class="btn pixel-corners exit-btn" onclick={handleReturnToLobbies}>
				{m.game_hud_exit({}, { locale: storeI18n.locale })}
			</button>
		{/if}
	</div>
{/if}

<style>
	.hud-container {
		color: var(--table-text);
		font-weight: bold;
		font-size: 1.2rem;
		display: flex;
		align-items: center;
		gap: 15px;
		min-width: 0;
	}

	/* Narrow screens: everything stays on ONE row, with the turn-order strip
	   taking the slack and scrolling sideways inside it (see TurnOrderStrip).
	   Wrapping instead used to push Exit onto a second line, and on a phone the
	   HUD's height comes straight out of the board's. */
	@media (max-width: 700px) {
		.hud-container {
			width: 100%;
			gap: 8px;
			font-size: 1rem;
		}

		.timer {
			flex: none;
			padding: 3px 6px;
			font-size: 0.8rem;
		}

		.exit-btn {
			flex: none;
			padding: 4px 10px;
			font-size: 0.8rem;
		}
	}

	.collapse-toggle {
		border: none;
		background: transparent;
		color: var(--table-text);
		cursor: pointer;
		font-size: 1rem;
		line-height: 1;
		padding: 4px;
	}

	.timer {
		padding: 4px 10px;
		font-family: monospace;
		transition: background-color 0.3s ease;
	}

	/* Size override on top of the shared flat .btn */
	.exit-btn {
		padding: 6px 14px;
		font-weight: bold;
		font-size: 0.9rem;
	}

	.spectating-badge {
		padding: 3px 8px;
		background: var(--brand-2, #6366f1);
		color: #fff;
		font-size: 0.75rem;
		text-transform: uppercase;
		letter-spacing: 0.05em;
		display: inline-flex;
		align-items: center;
	}

	.spectator-counter {
		padding: 3px 8px;
		background: var(--surface-2);
		font-size: 0.8rem;
		display: inline-flex;
		align-items: center;
		gap: 4px;
	}

	.elimination-standings {
		display: flex;
		gap: 4px;
		background: var(--surface-2);
		padding: 3px 8px;
		font-size: 0.75rem;
	}

	.placement-chip {
		color: var(--warning, #f59e0b);
	}
</style>
