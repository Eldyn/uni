<script lang="ts">
	import { onMount } from "svelte";
	import { storeStats } from "../../stores/stats.svelte";
	import { storeNavigation } from "../../stores/navigation.svelte";

	onMount(() => {
		storeStats.fetchMe();
		storeStats.fetchLeaderboard();
	});

	function getWinRate(wins: number, losses: number): string {
		const total = wins + losses;
		if (total === 0) return "0%";
		return Math.round((wins / total) * 100) + "%";
	}
</script>

<div class="doodle-bg"></div>

<div class="stats-container">
	<header class="top-bar">
		<button class="styled-btn pixel-corners" onclick={() => storeNavigation.back()}>
			↩ Back
		</button>
		<h1>GLOBAL LEADERBOARD</h1>
		<button class="styled-btn pixel-corners" onclick={() => storeNavigation.goto("detailedStats")}>
			Card Arsenal
		</button>
	</header>

	<div class="content">
		{#if storeStats.leaderboard.length > 0}
			{@const topPlayer = storeStats.leaderboard[0]}
			<div class="top-player-card pixel-corners">
				<div class="profile-section">
					<h2>👑 {topPlayer.username}</h2>
					<span class="rank-badge pixel-corners">#1 Global</span>
				</div>
				<div class="stats-grid">
					<div class="stat-box">
						<span class="stat-label">Wins</span>
						<span class="stat-value text-green">{topPlayer.total_wins}</span>
					</div>
					<div class="stat-box">
						<span class="stat-label">Losses</span>
						<span class="stat-value text-red">{topPlayer.total_losses}</span>
					</div>
					<div class="stat-box">
						<span class="stat-label">Win Rate</span>
						<span class="stat-value text-purple">
							{getWinRate(topPlayer.total_wins, topPlayer.total_losses)}
						</span>
					</div>
				</div>
			</div>
		{/if}

		<div class="leaderboard-section pixel-corners">
			<h3>Top 50 Players</h3>

			{#if storeStats.isLoading}
				<p class="loading-text">Loading rankings...</p>
			{:else if storeStats.leaderboard.length === 0}
				<p class="loading-text">No games have been played yet!</p>
			{:else}
				<div class="leaderboard-list">
					{#each storeStats.leaderboard as player, index}
						<div
							class="leaderboard-row pixel-corners"
							class:is-me={player.username === storeStats.myStats?.username}
						>
							<div class="rank-col" class:first={index === 0} class:second>
								#{player.rank}
							</div>
							<div class="name-col">{player.username}</div>
							<div class="score-col">
								<span class="wins">{player.total_wins}W</span> -
								<span class="losses">{player.total_losses}L</span>
							</div>
						</div>
					{/each}

					{#if storeStats.myStats && (storeStats.myStats.rank === null || storeStats.myStats.rank > storeStats.leaderboard.length)}
						<div class="sticky-divider"></div>
						<div class="leaderboard-row is-me sticky-me pixel-corners">
							<div class="rank-col">
								#{storeStats.myStats.rank ? storeStats.myStats.rank : "?"}
							</div>
							<div class="name-col">{storeStats.myStats.username} (You)</div>
							<div class="score-col">
								<span class="wins">{storeStats.myStats.total_wins}W</span> -
								<span class="losses">{storeStats.myStats.total_losses}L</span>
							</div>
						</div>
					{/if}
				</div>
			{/if}
		</div>
	</div>
</div>

<style>
	.styled-btn {
		padding: 8px 16px;
		background: var(--accent);
		color: white;
		border: none;
		border-radius: 6px; /* Retained as requested */
		font-size: 14px;
		font-weight: 500;
		cursor: pointer;
		transition: opacity 0.2s;
		font-family: "Pixel", sans-serif;
	}
	.styled-btn:hover {
		opacity: 0.8;
	}

	.top-bar h1 {
		margin: 0;
		font-size: 1.5rem;
		color: var(--accent); /* Updated */
		text-shadow: 2px 2px 0px #000;
	}

	/* Update highlight references */
	.top-player-card {
		background: #2a2a2d;
		padding: 24px;
		box-shadow: inset 0 0 0 4px var(--accent); /* Updated */
		position: relative;
	}
	/* Global Background Element */
	.doodle-bg {
		position: fixed;
		inset: 0;
		background-color: #121212;
		/* Add your doodle texture here: */
		background-image: url("/assets/bg_stats.png");
		background-size: cover;
		z-index: 0;
	}

	/* Pixelated Corners Trick */
	.pixel-corners {
		/* Removes native smooth rounding */
		border-radius: 0 !important;
		/* Cuts the corners in 4-pixel blocky steps */
		clip-path: polygon(
			0 4px,
			4px 4px,
			4px 0,
			calc(100% - 4px) 0,
			calc(100% - 4px) 4px,
			100% 4px,
			100% calc(100% - 4px),
			calc(100% - 4px) calc(100% - 4px),
			calc(100% - 4px) 100%,
			4px 100%,
			4px calc(100% - 4px),
			0 calc(100% - 4px)
		);
	}

	.profile-section h2,
	.top-bar h1,
	.back-btn {
		line-height: 1.3;
		padding-top: 2px;
		font-family: "Pixel", sans-serif; /* Ensuring blocky font if available */
	}

	.stats-container {
		position: relative;
		z-index: 1; /* Sits above the doodle background */
		display: flex;
		flex-direction: column;
		height: 100vh;
		color: white;
	}

	.top-bar {
		display: flex;
		justify-content: space-between;
		align-items: center;
		padding: 20px;
		background-color: #1c1c1e;
		border-bottom: 4px solid #333; /* Thicker border for pixel look */
	}

	.top-bar h1 {
		margin: 0;
		font-size: 1.5rem;
		color: #ffcc00;
		text-shadow: 2px 2px 0px #000;
	}

	.back-btn {
		background: #1c1c1e;
		color: #ffcc00;
		/* Using box-shadow as a border because clip-path cuts real borders */
		box-shadow: inset 0 0 0 2px #ffcc00;
		border: none;
		padding: 8px 16px;
		cursor: pointer;
		font-weight: bold;
		transition: filter 0.2s;
	}

	.back-btn:hover {
		filter: brightness(1.2);
	}

	.content {
		flex: 1;
		overflow-y: auto;
		padding: 20px;
		max-width: 800px;
		margin: 0 auto;
		width: 100%;
		display: flex;
		flex-direction: column;
		gap: 30px;
		padding-bottom: 60px; /* Space for the sticky element */
	}

	.top-player-card {
		background: #2a2a2d;
		padding: 24px;
		box-shadow: inset 0 0 0 4px var(--accent);
		position: relative;
	}

	.profile-section {
		display: flex;
		justify-content: space-between;
		align-items: center;
		margin-bottom: 20px;
		border-bottom: 4px solid var(--accent);
		padding-bottom: 15px;
	}

	.profile-section h2 {
		margin: 0;
		font-size: 1.8rem;
		color: var(--accent);
		text-shadow: 2px 2px 0px #000;
	}

	.rank-badge {
		background: var(--accent);
		color: #000;
		padding: 8px 16px;
		font-weight: 900;
		font-size: 1.1rem;
	}

	.stats-grid {
		display: flex;
		justify-content: space-around;
		text-align: center;
	}

	.stat-box {
		display: flex;
		flex-direction: column;
		gap: 8px;
	}
	.stat-label {
		color: #8e8e93;
		font-size: 0.9rem;
		text-transform: uppercase;
		letter-spacing: 1px;
	}
	.stat-value {
		font-size: 2rem;
		font-weight: bold;
		text-shadow: 2px 2px 0px #000;
	}
	.text-green {
		color: #34c759;
	}
	.text-red {
		color: #ff3b30;
	}
	.text-purple {
		color: #9b5de5;
	} /* Updated from blue to purple */

	/* --- Leaderboard List --- */
	.leaderboard-section {
		background: #1c1c1e;
		padding: 20px;
		box-shadow: inset 0 0 0 4px #333;
		position: relative;
	}

	.leaderboard-section h3 {
		margin-top: 0;
		color: var(--accent);
		margin-bottom: 15px;
		text-shadow: 2px 2px 0px #000;
	}

	.leaderboard-list {
		display: flex;
		flex-direction: column;
		gap: 8px;
	}

	.leaderboard-row {
		display: flex;
		align-items: center;
		background: #2a2a2d;
		padding: 12px 16px;
		transition: transform 0.1s;
	}

	.leaderboard-row:hover {
		transform: scale(1.01);
		background: #323236;
	}

	.leaderboard-row.is-me {
		background: #3a1b5c; /* Dark purple background */
		box-shadow: inset 0 0 0 4px #9b5de5; /* Purple border */
	}

	.rank-col {
		width: 50px;
		font-weight: bold;
		color: #8e8e93;
		font-size: 1.2rem;
	}
	.name-col {
		flex: 1;
		font-weight: 600;
		font-size: 1.1rem;
	}
	.score-col {
		font-weight: bold;
		font-size: 1.1rem;
		text-shadow: 1px 1px 0px #000;
	}
	.wins {
		color: #34c759;
	}
	.losses {
		color: #ff3b30;
	}

	/* Sticky specific styles */
	.sticky-divider {
		height: 12px; /* Space between end of list and sticky block */
	}

	.sticky-me {
		position: sticky;
		bottom: -20px; /* Snaps to bottom of the content container */
		margin-top: auto;
		z-index: 10;
		/* Dropshadow to show it's floating above */
		filter: drop-shadow(0px -4px 6px rgba(0, 0, 0, 0.6));
	}

	.loading-text {
		text-align: center;
		color: #8e8e93;
		font-style: italic;
	}
</style>
