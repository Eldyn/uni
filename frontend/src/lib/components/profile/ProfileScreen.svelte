<script lang="ts">
	import { onMount } from "svelte";
	import { storeNavigation } from "$stores/navigation.svelte";
	import { storeAuth } from "$stores/auth.svelte";
	import { storeStats } from "$stores/stats.svelte";
	import { storeI18n } from "$stores/i18n.svelte";
	import * as m from "$lib/paraglide/messages.js";
	import Avatar from "$components/common/Avatar.svelte";
	import VerifyCodeForm from "$components/auth/VerifyCodeForm.svelte";

	let showVerify = $state(false);

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
				<p class="font-tiny text-xs uppercase text-text">
					{m.leaderboard_wins({}, { locale: storeI18n.locale })}
				</p>
				<p class="title-stats">{storeStats.myStats.total_wins}</p>
			</div>
			<div>
				<p class="font-tiny text-xs uppercase text-text">
					{m.leaderboard_losses({}, { locale: storeI18n.locale })}
				</p>
				<p class="title-stats">{storeStats.myStats.total_losses}</p>
			</div>
			<div>
				<p class="font-tiny text-xs uppercase text-text">
					{m.profile_rank({}, { locale: storeI18n.locale })}
				</p>
				<p class="title-stats">{storeStats.myStats.rank ?? "?"}</p>
			</div>
		</div>
	{/if}

	<div class="flex flex-col gap-3">
		{#if storeAuth.isLoggedIn && !storeAuth.emailVerified}
			<button class="btn-secondary px-4 py-3 text-left" onclick={() => (showVerify = !showVerify)}>
				{m.verify_profile_button({}, { locale: storeI18n.locale })}
			</button>
			{#if showVerify}
				<VerifyCodeForm onVerified={() => (showVerify = false)} />
			{/if}
		{/if}
		<button class="btn-secondary px-4 py-3 text-left" onclick={() => storeNavigation.goto("stats")}>
			{m.profile_leaderboard_button({}, { locale: storeI18n.locale })}
		</button>
		<button
			class="btn-secondary px-4 py-3 text-left"
			onclick={() => storeNavigation.goto("settings")}
		>
			{m.profile_settings_button({}, { locale: storeI18n.locale })}
		</button>
	</div>
</div>
