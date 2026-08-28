<script lang="ts">
	import { onMount } from "svelte";
	import { storeStats } from "$stores/stats.svelte";
	import { storeNavigation } from "$stores/navigation.svelte";

	onMount(() => {
		storeStats.fetchMe();
		storeStats.fetchLeaderboard();
	});

	function getWinRate(wins: number, losses: number): string {
		const total = wins + losses;
		if (total === 0) return "0%";
		return Math.round((wins / total) * 100) + "%";
	}

	// Podium tints. Gold is the one that exists as a theme token; silver and
	// bronze are podium-only and have no equivalent in the palette, so they
	// stay local rather than borrowing an unrelated card colour.
	function rankClass(rank: number): string {
		if (rank === 1) return "text-gold";
		if (rank === 2) return "rank-silver";
		if (rank === 3) return "rank-bronze";
		return "text-text/50";
	}
</script>

<div class="doodle-bg"></div>

<div class="relative z-1 flex h-[100svh] flex-col text-white">
	<header
		class="flex shrink-0 items-center justify-between gap-2 border-b-2 border-border bg-bg px-3 py-2 sm:px-6 sm:py-3 lg:px-10 max-lg:landscape:py-1"
	>
		<button
			class="btn pixel-corners shrink-0 px-3 py-2 sm:px-4"
			onclick={() => storeNavigation.goto("main")}
			aria-label="Back"
		>
			<i class="hn pix hn-arrow-left text-lg leading-none sm:hidden"></i>
			<span class="hidden uppercase sm:inline">Back</span>
		</button>

		<!-- Scales instead of truncating: "GLOBAL LEADERBOARD" is long enough that
		     a fixed size ellipsised to "GLO…" between the two buttons on a phone.
		     Same ladder idiom as LobbyBrowse's header, retuned for the longer
		     string ('!' because .title-screen hardcodes 56px). -->
		<h1
			class="title-screen shrink-0 text-center text-lg sm:text-2xl lg:text-3xl max-lg:text-lg! max-lg:landscape:text-base!"
		>
			GLOBAL LEADERBOARD
		</h1>

		<button
			class="btn pixel-corners shrink-0 px-3 py-2 sm:px-4"
			onclick={() => storeNavigation.goto("detailedStats")}
			aria-label="Card Arsenal"
		>
			<i class="hn pix hn-viewblocks text-lg leading-none sm:hidden"></i>
			<span class="hidden uppercase sm:inline">Arsenal</span>
		</button>
	</header>

	<div
		class="scrollbar-accent mx-auto flex w-full max-w-3xl flex-1 flex-col gap-4 overflow-y-auto px-3 py-4 pb-12 sm:gap-6 sm:px-5"
	>
		{#if storeStats.leaderboard.length > 0}
			{@const topPlayer = storeStats.leaderboard[0]}
			<div class="panel pixel-corners p-4 shadow-[inset_0_0_0_4px_var(--accent)] sm:p-6">
				<!-- Crown, name and badge stack centred on phones (portrait and
				     landscape) where a split row reads as two stray fragments; from
				     `lg` up there's room for the original spread row. -->
				<div
					class="mb-4 flex flex-col items-center gap-2 border-b-4 border-accent pb-3 lg:flex-row lg:justify-between"
				>
					<h2
						class="flex min-w-0 items-center justify-center gap-2 font-pixel text-xl text-accent [text-shadow:2px_2px_0_var(--pixel-shadow)] sm:text-2xl"
					>
						<i class="hn pix hn-crown shrink-0 text-gold"></i>
						<span class="truncate">{topPlayer.username}</span>
					</h2>
					<span
						class="pypx-thick pixel-corners shrink-0 bg-accent px-3 py-1.5 text-xs uppercase text-black sm:text-sm"
					>
						#1 In the world!
					</span>
				</div>

				<div class="grid grid-cols-3 gap-2 text-center">
					<div class="flex flex-col gap-1">
						<span class="font-tiny text-[0.7rem] uppercase tracking-wide text-text/50">Wins</span>
						<span class="stat-value text-success">{topPlayer.total_wins}</span>
					</div>
					<div class="flex flex-col gap-1">
						<span class="font-tiny text-[0.7rem] uppercase tracking-wide text-text/50">Losses</span>
						<span class="stat-value text-danger">{topPlayer.total_losses}</span>
					</div>
					<div class="flex flex-col gap-1">
						<span class="font-tiny text-[0.7rem] uppercase tracking-wide text-text/50">W/L</span>
						<span class="stat-value text-accent">
							{getWinRate(topPlayer.total_wins, topPlayer.total_losses)}
						</span>
					</div>
				</div>
			</div>
		{/if}

		<div class="panel pixel-corners flex flex-col p-3 shadow-[inset_0_0_0_4px_#333] sm:p-5">
			<h3
				class="pypx-thick mb-3 text-base text-accent [text-shadow:2px_2px_0_var(--pixel-shadow)] sm:text-lg"
			>
				TOP 50 PLAYERS
			</h3>

			{#if storeStats.isLoading}
				<p class="py-6 text-center font-tiny text-sm text-text/50">Loading leaderboard...</p>
			{:else if storeStats.leaderboard.length === 0}
				<p class="py-6 text-center font-tiny text-sm text-text/50">
					Something seems to have gone wrong!
				</p>
			{:else}
				<ul class="flex list-none flex-col gap-2 p-0">
					{#each storeStats.leaderboard as player (player.username)}
						<li
							class="row pixel-corners grid grid-cols-[auto_1fr_auto] items-center gap-2 px-2.5 py-2 sm:gap-3 sm:px-4 sm:py-3"
							class:is-me={player.username === storeStats.myStats?.username}
						>
							<span
								class="w-9 shrink-0 font-pixel text-sm sm:w-12 sm:text-base {rankClass(
									player.rank
								)}"
							>
								#{player.rank}
							</span>
							<span class="min-w-0 truncate font-tiny text-sm text-white sm:text-base">
								{player.username}
							</span>
							<span
								class="pypx-thick shrink-0 whitespace-nowrap text-xs [text-shadow:1px_1px_0_var(--pixel-shadow)] sm:text-sm"
							>
								<span class="text-success">{player.total_wins}W</span>
								<span class="text-text/40">-</span>
								<span class="text-danger">{player.total_losses}L</span>
							</span>
						</li>
					{/each}

					{#if storeStats.myStats && (storeStats.myStats.rank === null || storeStats.myStats.rank > storeStats.leaderboard.length)}
						<li aria-hidden="true" class="h-3"></li>
						<li
							class="row is-me sticky-me pixel-corners grid grid-cols-[auto_1fr_auto] items-center gap-2 px-2.5 py-2 sm:gap-3 sm:px-4 sm:py-3"
						>
							<span class="w-9 shrink-0 font-pixel text-sm text-text/50 sm:w-12 sm:text-base">
								#{storeStats.myStats.rank ? storeStats.myStats.rank : "?"}
							</span>
							<span class="min-w-0 truncate font-tiny text-sm text-white sm:text-base">
								{storeStats.myStats.username} (You)
							</span>
							<span
								class="pypx-thick shrink-0 whitespace-nowrap text-xs [text-shadow:1px_1px_0_var(--pixel-shadow)] sm:text-sm"
							>
								<span class="text-success">{storeStats.myStats.total_wins}W</span>
								<span class="text-text/40">-</span>
								<span class="text-danger">{storeStats.myStats.total_losses}L</span>
							</span>
						</li>
					{/if}
				</ul>
			{/if}
		</div>
	</div>
</div>

<style>
	/* This screen is pinned dark in both themes — it's laid over the bg_stats.png
	   pixel art, which is dark art, so the panel fills below are flat values on
	   purpose rather than theme tokens that would turn cream in light mode and
	   take the white text with them. Same rule as --table in app.css and as
	   DetailedStatsScreen. Everything semantic (accent, gold, win/loss) still
	   goes through the palette. */
	.doodle-bg {
		position: fixed;
		top: 0;
		left: 0;
		width: 100%;
		height: 100svh;
		background-color: #121212;
		background-image: url("/assets/bg_stats.png");
		image-rendering: pixelated;
		background-size: cover;
		z-index: 0;
	}

	.panel {
		background: #1c1c1e;
	}

	/* Pypx at 800 — the thick cut (pypx-thick.woff2), the heaviest face the
	   family ships. Carries the numbers and rank callouts (the #1 badge, the
	   list heading, each row's W-L) so the scoring reads as scoreboard type
	   against the lighter --tiny face used for player names. */
	.pypx-thick {
		font-family: var(--pypx);
		font-weight: 800;
	}

	.stat-value {
		font-family: var(--pixel);
		font-size: clamp(1.25rem, 5vw, 2rem);
		font-weight: bold;
		text-shadow: 2px 2px 0 var(--pixel-shadow);
	}

	.row {
		background: #2a2a2d;
		transition: background 0.1s ease;
	}
	.row:hover {
		background: #323236;
	}
	.row.is-me {
		background: #3a1b5c;
		box-shadow: inset 0 0 0 4px var(--accent);
	}

	.rank-silver {
		color: #d4d4d8;
	}
	.rank-bronze {
		color: #cd7f32;
	}

	/* Pins the viewer's own row to the bottom of the scroll area when they rank
	   outside the visible top 50, so it stays readable while they scroll. */
	.sticky-me {
		position: sticky;
		bottom: -1rem;
		margin-top: auto;
		z-index: 10;
		filter: drop-shadow(0 -4px 6px rgba(0, 0, 0, 0.6));
	}

	@media (prefers-reduced-motion: reduce) {
		.row {
			transition: none;
		}
	}
</style>
