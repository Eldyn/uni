<script lang="ts">
	import { storeGame } from "$stores/game.svelte";
	import { storeLobby } from "$stores/lobby.svelte";
	import ExitConfirmModal from "./ExitConfirmModal.svelte";
	import { storeI18n } from "$stores/i18n.svelte";
	import * as m from "$lib/paraglide/messages.js";
	import { formatTurnTimer } from "$utils/timeFormat";

	let showExitConfirm = $state(false);

	function handleExitClick() {
		showExitConfirm = true;
	}

	function confirmExit() {
		showExitConfirm = false;
		storeLobby.leave();
	}

	function cancelExit() {
		showExitConfirm = false;
	}
</script>

{#if storeGame.state && !storeGame.activePrompt && !storeGame.activeWindow}
	<div class="hud-container">
		<div class="hud-bar pixel-bordered">
			{#if storeGame.isSpectator}
				<span class="spectating-badge pixel-corners">
					{m.game_spectating_indicator({}, { locale: storeI18n.locale })}
				</span>
			{/if}

			{#if storeGame.povSpectatorCount > 0}
				<span
					class="spectator-counter pixel-corners"
					title={m.game_spectator_count(
						{ count: storeGame.povSpectatorCount },
						{ locale: storeI18n.locale }
					)}
				>
					<i class="hn hn-eye pix"></i>
					{storeGame.povSpectatorCount}
				</span>
			{/if}

			<div class="timer-block">
				<span
					class="timer pixel-corners"
					style="background: {storeGame.turnTimeRemaining <= 5
						? 'var(--danger)'
						: 'var(--surface-2)'};"
				>
					{formatTurnTimer(storeGame.turnTimeRemaining)}
				</span>
				<span class="gamemode-label">
					{storeGame.state?.mode === "race"
						? m.lobby_mode_race({}, { locale: storeI18n.locale })
						: m.lobby_mode_standard({}, { locale: storeI18n.locale })}
				</span>
				{#if storeLobby.current?.settings?.ranked}
					<span class="ranked-badge"
						>{m.game_hud_ranked_badge({}, { locale: storeI18n.locale })}</span
					>
				{/if}
			</div>

			{#if !storeGame.state?.is_over}
				<button class="exit-btn pixel-corners" onclick={handleExitClick}>
					{m.game_hud_exit({}, { locale: storeI18n.locale })}
				</button>
			{/if}
		</div>
	</div>

	<ExitConfirmModal open={showExitConfirm} onConfirm={confirmExit} onCancel={cancelExit} />
{/if}

<style>
	.hud-container {
		color: var(--table-text);
		font-weight: bold;
		font-size: 1rem;
		display: flex;
		align-items: center;
		gap: 10px;
		min-width: 0;
	}

	/* The HUD reads as one dark pixel panel floating over the board (see the
	   reference art): near-black fill, 2px border, and a hard south offset
	   shadow. drop-shadow (not box-shadow) so the shadow follows the notched
	   clip-path, exactly as TooltipCard does. */
	.hud-bar {
		display: flex;
		align-items: center;
		gap: var(--space-2);
		padding: var(--space-1) var(--space-2);
		--pc-fill: var(--surface-deep);
		--pc-border: var(--border);
		--pc-width: 2px;
		color: var(--table-text);
		filter: drop-shadow(0 4px 0 var(--pixel-shadow));
		min-width: 0;
	}

	/* Narrow screens: everything stays on ONE row. Wrapping instead used to
	   push Exit onto a second line, and on a phone the HUD's height comes
	   straight out of the board's. */
	@media (max-width: 700px) {
		.hud-container {
			width: 100%;
			gap: 6px;
			font-size: 0.9rem;
		}

		.hud-bar {
			gap: var(--space-2);
			padding: var(--space-1) var(--space-2);
		}

		.timer {
			flex: none;
			padding: 4px 6px;
			font-size: 0.95rem;
		}

		.exit-btn {
			flex: none;
			padding: 6px 10px;
			font-size: 0.75rem;
		}

		/* Timer pinned left, exit pinned right — everything else (spectator
		   count, mode/ranked badges) keeps its DOM order between them instead of
		   fighting for either corner. */
		.timer-block {
			order: -1;
		}

		.exit-btn {
			order: 99;
			margin-left: auto;
		}

		/* SpectatorBanner already announces "SPECTATING" prominently below the
		   HUD row (see GameScreen.svelte) — this inline badge only duplicated it
		   and was part of what made the top of the screen unreadable. */
		.spectating-badge {
			display: none;
		}
	}

	.timer-block {
		display: flex;
		flex-direction: column;
		align-items: center;
		justify-content: center;
		gap: 2px;
		flex: none;
	}

	/* Pixel digits on a dark tile: the reference's chunky readout, and the
	   only element in the bar allowed to grow. A fixed min-width keeps the
	   chip from resizing as the digits change ("0:00" vs "12:34"), which made
	   the whole HUD jitter on every tick of the timer. */
	.timer {
		padding: 5px 8px;
		min-width: 4ch;
		text-align: center;
		font-variant-numeric: tabular-nums;
		font-family: var(--pixel);
		font-size: 1rem;
		line-height: 1;
		letter-spacing: 0.02em;
		color: var(--text-h);
		transition: background-color 0.3s ease;
	}

	/* Mode and ranked badge stack vertically so the timer block fills the
	   taller avatar row instead of running wide beside it. */
	.gamemode-label {
		font-family: var(--tiny);
		font-size: 0.6rem;
		line-height: 1;
		text-transform: uppercase;
		letter-spacing: 0.06em;
		color: var(--text);
		text-align: center;
		white-space: nowrap;
	}

	.ranked-badge {
		font-family: var(--tiny);
		font-size: 0.6rem;
		line-height: 1;
		text-transform: uppercase;
		letter-spacing: 0.06em;
		color: var(--accent);
		white-space: nowrap;
	}

	/* Blocky red Exit, matching the reference's chunky arcade button. */
	.exit-btn {
		font-family: var(--pixel);
		font-weight: bold;
		font-size: 0.85rem;
		color: #fff;
		background: var(--danger);
		border: none;
		padding: 6px 12px;
		cursor: pointer;
		flex: none;
		filter: drop-shadow(0 3px 0 var(--pixel-shadow));
		transition: filter 0.12s ease;
	}
	.exit-btn:hover:not(:disabled) {
		filter: brightness(1.12) drop-shadow(0 3px 0 var(--pixel-shadow));
	}

	.spectating-badge {
		padding: 3px 6px;
		background: var(--accent-violet);
		color: #fff;
		font-family: var(--tiny);
		font-size: 0.62rem;
		text-transform: uppercase;
		letter-spacing: 0.05em;
		display: inline-flex;
		align-items: center;
		flex: none;
		filter: drop-shadow(0 2px 0 var(--pixel-shadow));
	}

	.spectator-counter {
		padding: 3px 6px;
		background: var(--surface-2);
		font-family: var(--tiny);
		font-size: 0.7rem;
		display: inline-flex;
		align-items: center;
		gap: 4px;
		flex: none;
	}
</style>
