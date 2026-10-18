<script lang="ts">
	/**
	 * @file ProfileScreen.svelte
	 * @brief Thin wrapper for the owner's profile: reads the stores, fetches
	 * the stats and hands the reusable ProfileView its props and snippets.
	 */
	import { onMount } from "svelte";
	import { storeNavigation } from "$stores/navigation.svelte";
	import { storeAuth } from "$stores/auth.svelte";
	import { storeStats } from "$stores/stats.svelte";
	import { storeAvatar } from "$stores/avatar.svelte";
	import ProfileView from "$components/profile/ProfileView.svelte";
	import ProfileAccount from "$components/profile/ProfileAccount.svelte";
	import ProfileLogout from "$components/profile/ProfileLogout.svelte";

	type ProfileViewStatus = "loading" | "ready" | "error" | "guest";

	let loadFailed = $state(false);

	async function loadStats() {
		if (!storeAuth.isLoggedIn) return;
		loadFailed = false;
		await storeStats.fetchMe();
		if (!storeStats.myStats) loadFailed = true;
	}

	onMount(() => {
		if (!storeAuth.isLoggedIn) return;
		storeAvatar.fetchMine();
		if (!storeStats.myStats) {
			loadStats();
		}
	});

	const status = $derived.by((): ProfileViewStatus => {
		if (!storeAuth.isLoggedIn) return "guest";
		if (storeStats.myStats) return "ready";
		if (storeStats.isLoading) return "loading";
		if (loadFailed) return "error";
		return "loading";
	});

	const profile = $derived({
		username: storeAuth.username,
		loadout: storeAvatar.loadout,
		stats: storeStats.myStats
	});
</script>

<div class="mx-auto flex w-full max-w-3xl flex-1 flex-col p-6">
	<ProfileView
		{profile}
		isOwn={true}
		{status}
		onRetry={loadStats}
		onLeaderboard={() => storeNavigation.goto("stats")}
		onRegister={() => storeNavigation.gotoAuth("register")}
		oneditlook={() => storeNavigation.goto("wardrobe")}
	>
		{#snippet account()}
			<ProfileAccount />
		{/snippet}
		{#snippet accountAction()}
			<ProfileLogout />
		{/snippet}
	</ProfileView>
</div>
