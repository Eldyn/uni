<script lang="ts">
	import Modal from "$lib/components/common/Modal.svelte";
	import TintedSprite from "$lib/components/common/TintedSprite.svelte";
	import GameInterruptedPopup from "./popup/GameInterruptedPopup.svelte";
	import { storeGame } from "$stores/game.svelte";
	import { storeAuth } from "$stores/auth.svelte";
	import { storeAudio } from "$stores/audio.svelte";
	import { BOT_COLOR, playerColorFor } from "$lib/palette";
	import { storeI18n } from "$stores/i18n.svelte";
	import * as m from "$lib/paraglide/messages.js";

	let isInterrupted = $derived(storeGame.state?.is_over && !storeGame.state?.winner);
	let isVictory = $derived(!!storeGame.state?.winner);
	let isElimination = $derived(storeGame.state?.mode === "elimination");

	let winnerName = $derived(storeGame.state?.winner ?? "Unknown");
	let isMe = $derived(winnerName === storeAuth.username);

	let myRank = $derived.by(() => {
		const me = storeAuth.username;
		if (!me) return -1;
		const idx = storeGame.placements.indexOf(me);
		return idx === -1 ? -1 : idx + 1;
	});
	let isPodium = $derived(isElimination && !isMe && (myRank === 2 || myRank === 3));

	let winnerIdx = $derived(
		storeGame.state?.players?.findIndex((p) => p.username === winnerName) ?? -1
	);
	let winnerIsBot = $derived(
		winnerIdx !== -1 ? (storeGame.state?.players?.[winnerIdx]?.is_bot ?? false) : false
	);
	let winnerColor = $derived(winnerIsBot ? BOT_COLOR : playerColorFor(winnerIdx));

	function colorForRankedName(name: string): string {
		const idx = storeGame.state?.players?.findIndex((p) => p.username === name) ?? -1;
		const isBot = idx !== -1 ? (storeGame.state?.players?.[idx]?.is_bot ?? false) : false;
		return isBot ? BOT_COLOR : playerColorFor(idx);
	}

	let podium = $derived(storeGame.placements.slice(0, 3));

	let hasPlayedResultSfx = $state(false);
	$effect(() => {
		if (!isVictory || hasPlayedResultSfx) return;
		hasPlayedResultSfx = true;
		if (isMe || isPodium) {
			// PLACEHOLDER-SFX: sfx.match.victory
			storeAudio.playSfx("sfx.match.victory");
		} else {
			// PLACEHOLDER-SFX: sfx.match.defeat
			storeAudio.playSfx("sfx.match.defeat");
		}
	});
</script>

{#if storeGame.state?.is_over}
	{#if isInterrupted}
		<GameInterruptedPopup />
	{:else if isVictory}
		<Modal
			open={true}
			dismissible={false}
			titleId="end-title"
			contentClass="end-content pixel-corners"
		>
			<h1
				id="end-title"
				class="result {isMe ? 'result--win' : isPodium ? 'result--podium' : 'result--lose'}"
			>
				{isMe
					? m.game_victory_title({}, { locale: storeI18n.locale })
					: isPodium
						? m.game_podium_finish_title({}, { locale: storeI18n.locale })
						: m.game_defeat_title({}, { locale: storeI18n.locale })}
			</h1>

			<div class="avatar-stage">
				<div class="avatar-glow"></div>
				<div class="avatar-frame">
					<TintedSprite src="/assets/base_player.gif" color={winnerColor} fit="contain" size={96} />
					<img class="crown" src="/assets/crown_host.gif" alt="Winner crown" />
				</div>
			</div>

			<p class="winner-line">
				{m.game_winner_label({ name: winnerName }, { locale: storeI18n.locale })}
			</p>

			{#if isElimination && podium.length > 0}
				<div class="podium">
					{#each podium as name, i}
						<div class="podium-slot podium-slot--{i}">
							<div class="podium-avatar">
								<TintedSprite
									src="/assets/base_player.gif"
									color={colorForRankedName(name)}
									fit="contain"
									size={48}
								/>
								{#if i === 0}
									<img class="podium-crown" src="/assets/crown_host.gif" alt="" />
								{/if}
							</div>
							<div
								class="podium-block rank-{i === 0 ? 'gold' : i === 1 ? 'silver' : 'bronze'}"
								style="border-color: {colorForRankedName(name)};"
							>
								<span class="podium-rank-label">
									{i === 0
										? m.game_podium_first_label({}, { locale: storeI18n.locale })
										: i === 1
											? m.game_podium_second_label({}, { locale: storeI18n.locale })
											: m.game_podium_third_label({}, { locale: storeI18n.locale })}
								</span>
							</div>
							<span class="podium-name">{name}</span>
						</div>
					{/each}
				</div>

				{#if storeGame.placements.length > 3}
					<div class="elimination-results pixel-corners">
						<h2 class="standings-heading">
							{m.game_placement_standings({}, { locale: storeI18n.locale })}
						</h2>
						<ol class="standings-list">
							{#each storeGame.placements as name, i}
								<li class="standing-item {name === storeAuth.username ? 'is-local' : ''}">
									<span class="rank">#{i + 1}</span>
									<span class="name">{name}</span>
								</li>
							{/each}
						</ol>
					</div>
				{/if}
			{/if}

			<button type="button" class="btn pixel-corners" onclick={() => storeGame.returnToLobby()}>
				{m.game_back_to_lobby({}, { locale: storeI18n.locale })}
			</button>
		</Modal>
	{/if}
{/if}

<style>
	@keyframes slideDown {
		0% {
			transform: translateY(-50px) scale(0.9);
			opacity: 0;
		}
		100% {
			transform: translateY(0) scale(1);
			opacity: 1;
		}
	}

	:global(.end-content) {
		text-align: center;
		color: var(--text-h);
		max-width: max-content;
		width: 90%;
		box-sizing: border-box;
		display: flex;
		flex-direction: column;
		align-items: center;
		gap: 18px;
		animation: slideDown 0.4s cubic-bezier(0.175, 0.885, 0.32, 1.275);
		overflow-wrap: break-word;
		word-wrap: break-word;
	}

	.result {
		margin: 0;
		font-family: "FatPixel", sans-serif;
		font-size: 2.6rem;
		letter-spacing: 2px;
	}
	.result--win {
		color: var(--accent);
		text-shadow: 3px 3px 0px var(--pixel-shadow);
	}
	.result--lose {
		color: var(--danger);
		text-shadow: 3px 3px 0px var(--pixel-shadow);
	}
	.result--podium {
		color: var(--gold);
		text-shadow: 3px 3px 0px var(--pixel-shadow);
	}

	/* Stage gives the winner's icon the visual spotlight. */
	.avatar-stage {
		position: relative;
		width: 160px;
		height: 160px;
		display: flex;
		align-items: center;
		justify-content: center;
	}

	.avatar-glow {
		position: absolute;
		inset: -12px;
		background: radial-gradient(circle, var(--accent-bg) 0%, transparent 70%);
		animation: pulse 1.6s ease-in-out infinite;
	}

	@keyframes pulse {
		0%,
		100% {
			transform: scale(1);
			opacity: 0.7;
		}
		50% {
			transform: scale(1.15);
			opacity: 1;
		}
	}

	/* The crown gif is authored on the same 96x96 canvas as base_player.gif, so
	   the sprite and the crown render at the identical size in this frame and
	   overlap exactly (crown on the head). Overlay sprites must match the user
	   sprite's size, not exceed it — otherwise the crown appears to float. */
	.avatar-frame {
		position: relative;
		width: 96px;
		height: 96px;
		animation: float 2.2s ease-in-out infinite;
	}
	.crown {
		position: absolute;
		inset: 0;
		width: 100%;
		height: 100%;
		object-fit: contain;
		pointer-events: none;
	}

	@keyframes float {
		0%,
		100% {
			transform: translateY(0);
		}
		50% {
			transform: translateY(-6px);
		}
	}

	.winner-line {
		font-family: "Pixel", sans-serif;
		font-size: 1.15rem;
		margin: 0;
		line-height: 1.4;
	}
	.winner-name {
		color: var(--gold);
		text-shadow: 1px 1px 0px var(--pixel-shadow);
		font-weight: bold;
		font-size: 1.35rem;
		display: inline-block;
	}

	.elimination-results {
		width: 100%;
		background: var(--surface-2);
		padding: 10px 14px;
		box-sizing: border-box;
	}

	.standings-heading {
		font-size: 0.9rem;
		margin: 0 0 8px 0;
		color: var(--text-h);
		text-transform: uppercase;
		letter-spacing: 0.05em;
	}

	.standings-list {
		list-style: none;
		margin: 0;
		padding: 0;
		display: flex;
		flex-direction: column;
		gap: 4px;
	}

	.standing-item {
		display: flex;
		justify-content: space-between;
		font-size: 0.9rem;
		padding: 3px 8px;
		color: var(--text);
	}

	.standing-item.is-local {
		background: var(--surface-3);
		color: var(--brand, #38bdf8);
		font-weight: bold;
	}

	.standing-item .rank {
		font-weight: bold;
		color: var(--warning, #f59e0b);
	}

	/* Rank colors copied in locally — the .rank-silver/.rank-bronze
	   copies are scoped inside GameHud.svelte, so this component self-copies
	   all three to avoid depending on another component's scoped <style>. */
	.rank-gold {
		color: var(--gold);
	}
	.rank-silver {
		color: #d4d4d8;
	}
	.rank-bronze {
		color: #cd7f32;
	}

	.podium {
		display: flex;
		align-items: flex-end;
		justify-content: center;
		gap: 12px;
		margin: 8px 0;
	}

	.podium-slot {
		display: flex;
		flex-direction: column;
		align-items: center;
		gap: 4px;
	}

	.podium-block {
		width: 64px;
		border: 3px solid;
		border-radius: 4px 4px 0 0;
		display: flex;
		align-items: flex-end;
		justify-content: center;
		padding-bottom: 4px;
		box-sizing: border-box;
	}

	/* Varying tallness: 1st tallest, 2nd mid, 3rd shortest — plain rectangles,
	   explicitly a placeholder for real pedestal art later. */
	.podium-slot--0 .podium-block {
		height: 90px;
		background: var(--surface-3);
	}
	.podium-slot--1 .podium-block {
		height: 65px;
		background: var(--surface-2);
	}
	.podium-slot--2 .podium-block {
		height: 45px;
		background: var(--surface-2);
	}

	/* The podium avatar and its crown share one 48px box so the crown overlays
	   the sprite exactly, the same rule as the winner stage above. */
	.podium-avatar {
		position: relative;
		width: 48px;
		height: 48px;
	}

	.podium-crown {
		position: absolute;
		inset: 0;
		width: 100%;
		height: 100%;
		object-fit: contain;
		image-rendering: pixelated;
		pointer-events: none;
	}

	.podium-rank-label {
		font-weight: bold;
		font-size: 0.8rem;
		color: var(--text-h);
	}

	.podium-name {
		font-size: 0.75rem;
		max-width: 70px;
		overflow: hidden;
		text-overflow: ellipsis;
		white-space: nowrap;
	}
</style>
