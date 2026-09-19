<script lang="ts">
	import { storeGame } from "$stores/game.svelte";
	import { storeAuth } from "$stores/auth.svelte";
	import { storeLobby } from "$stores/lobby.svelte";
	import TurnOrderStrip from "./TurnOrderStrip.svelte";
	import ExitConfirmModal from "./ExitConfirmModal.svelte";
	import { storeI18n } from "$stores/i18n.svelte";
	import * as m from "$lib/paraglide/messages.js";
	import Tooltip from "$components/common/Tooltip.svelte";
	import RichText from "$components/common/RichText.svelte";
	import { getCardInfo } from "$lib/glossary/cardDescriptions";
	import { formatTurnTimer } from "$utils/timeFormat";

	let collapsed = $state(false);
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
					title={m.game_spectator_count(
						{ count: storeGame.spectatorCount },
						{ locale: storeI18n.locale }
					)}
				>
					<i class="hn hn-eye pix"></i>
					{storeGame.spectatorCount}
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
					{storeGame.state?.mode === "elimination"
						? m.lobby_mode_elimination({}, { locale: storeI18n.locale })
						: m.lobby_mode_standard({}, { locale: storeI18n.locale })}
					{#if storeLobby.current?.settings?.ranked}
						<span class="ranked-badge"
							>{m.game_hud_ranked_badge({}, { locale: storeI18n.locale })}</span
						>
					{/if}
				</span>
			</div>

			<TurnOrderStrip />

			{#if storeGame.state?.top_card}
				{@const topCard = storeGame.state.top_card}
				{@const cardInfo = getCardInfo(topCard, storeI18n.locale)}
				<Tooltip interactive={true} side="bottom">
					{#snippet tooltipContent()}
						<div class="top-card-tooltip">
							<div class="top-card-tooltip-title">{cardInfo.title}</div>
							<RichText text={cardInfo.description} allowKeywords={true} />
						</div>
					{/snippet}
					<div class="top-card-chip pixel-corners">
						<span class="top-card-dot {topCard.type}"></span>
						<span class="top-card-label">{cardInfo.title}</span>
					</div>
				</Tooltip>
			{/if}

			{#if storeGame.state?.mode === "elimination" && storeGame.placements.length > 0}
				<div
					class="elimination-standings pixel-corners"
					title={m.game_placement_standings({}, { locale: storeI18n.locale })}
				>
					{#each storeGame.placements as name, i}
						{@const rank = i + 1}
						{@const rankClass =
							storeGame.state?.is_over === true
								? rank === 1
									? "text-gold"
									: rank === 2
										? "rank-silver"
										: rank === 3
											? "rank-bronze"
											: "text-text/50"
								: "text-text/50"}
						<span
							class="placement-chip pixel-corners {rankClass}"
							class:is-me={name === storeAuth.username}
						>
							{m.game_placement_rank({ rank, name }, { locale: storeI18n.locale })}
						</span>
					{/each}
				</div>
			{/if}

			{#if !storeGame.state?.is_over}
				<button class="btn pixel-corners exit-btn" onclick={handleExitClick}>
					{m.game_hud_exit({}, { locale: storeI18n.locale })}
				</button>
			{/if}
		{/if}
	</div>

	<ExitConfirmModal open={showExitConfirm} onConfirm={confirmExit} onCancel={cancelExit} />
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
			gap: 6px;
			font-size: 1rem;
		}

		.timer {
			flex: none;
			padding: 4px 6px;
			font-size: 1rem;
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

	.timer-block {
		display: flex;
		flex-direction: column;
		align-items: center;
	}

	.gamemode-label {
		font-size: 0.65rem;
		text-transform: uppercase;
		letter-spacing: 0.04em;
		opacity: 0.75;
		display: block;
		margin-top: 2px;
	}

	.ranked-badge {
		color: var(--warning, #f59e0b);
		margin-left: 4px;
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
		display: inline-flex;
		align-items: center;
		gap: 4px;
		padding: 2px 8px;
		background: #2a2a2d;
		transition: background 0.1s ease;
	}

	.placement-chip:hover {
		background: #323236;
	}

	.placement-chip.is-me {
		background: #3a1b5c;
		box-shadow: inset 0 0 0 4px var(--accent);
	}

	.rank-silver {
		color: #d4d4d8;
	}

	.rank-bronze {
		color: #cd7f32;
	}

	.top-card-chip {
		display: inline-flex;
		align-items: center;
		gap: 6px;
		background: var(--surface-2);
		padding: 3px 8px;
		font-size: 0.8rem;
		cursor: help;
		user-select: none;
	}

	.top-card-dot {
		width: 8px;
		height: 8px;
		border-radius: 50%;
		display: inline-block;
		background: var(--text-h);
	}

	.top-card-dot.red {
		background: var(--redCard, #ef4444);
	}
	.top-card-dot.blue {
		background: var(--blueCard, #3b82f6);
	}
	.top-card-dot.green {
		background: var(--greenCard, #22c55e);
	}
	.top-card-dot.yellow {
		background: var(--yellowCard, #eab308);
	}
	.top-card-dot.white,
	.top-card-dot.black {
		background: #9333ea;
	}

	.top-card-tooltip {
		display: flex;
		flex-direction: column;
		gap: 4px;
	}

	.top-card-tooltip-title {
		font-weight: bold;
		font-size: 0.85rem;
		color: var(--warning, #f59e0b);
		text-transform: uppercase;
		letter-spacing: 0.05em;
	}
</style>
