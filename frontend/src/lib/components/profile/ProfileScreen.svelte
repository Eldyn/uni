<script lang="ts">
	import { onMount } from "svelte";
	import { storeNavigation } from "$stores/navigation.svelte";
	import { storeAuth } from "$stores/auth.svelte";
	import { storeStats } from "$stores/stats.svelte";
	import Avatar from "$components/common/Avatar.svelte";

	// storeStats.myStats is a PlayerStats (see stats.svelte.ts): username,
	// total_wins, total_losses, rank — there is no wins/matches_played field.

	onMount(() => {
		if (!storeStats.myStats) {
			storeStats.fetchMe();
		}
	});
</script>

<div class="flex flex-1 flex-col gap-6 p-6">
	<header class="flex items-center gap-4">
		<Avatar src={storeAuth.avatar} size={64} />
		<h1 class="title-screen">{storeAuth.username}</h1>
	</header>

	{#if storeStats.myStats}
		<div class="pixel-corners flex gap-6 bg-surface p-5 [--pc-border:var(--border)]">
			<div>
				<p class="font-tiny text-xs uppercase text-text">Wins</p>
				<p class="title-stats">{storeStats.myStats.total_wins}</p>
			</div>
			<div>
				<p class="font-tiny text-xs uppercase text-text">Losses</p>
				<p class="title-stats">{storeStats.myStats.total_losses}</p>
			</div>
			<div>
				<p class="font-tiny text-xs uppercase text-text">Rank</p>
				<p class="title-stats">{storeStats.myStats.rank ?? "?"}</p>
			</div>
		</div>
	{/if}

	<div class="flex flex-col gap-3">
		<button
			class="btn-secondary px-4 py-3 text-left"
			onclick={() => storeNavigation.goto("detailedStats")}
		>
			Leaderboard &amp; detailed stats
		</button>
		<button
			class="btn-secondary px-4 py-3 text-left"
			onclick={() => storeNavigation.goto("settings")}
		>
			Settings
		</button>
	</div>
</div>
