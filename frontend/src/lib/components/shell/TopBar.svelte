<script lang="ts">
	import type { Snippet } from "svelte";
	import { storeNavigation } from "$stores/navigation.svelte";
	import { storeAuth } from "$stores/auth.svelte";
	import { storeLobby } from "$stores/lobby.svelte";
	import Avatar from "$components/common/Avatar.svelte";
	import TextEffects from "$components/common/TextEffects.svelte";

	const SCREEN_TITLES: Partial<Record<string, string>> = {
		main: "UNI!",
		lobbies: "Browse",
		lobby: "Lobby",
		decks: "Decks",
		shop: "Shop",
		profile: "Profile",
		stats: "Profile",
		detailedStats: "Leaderboard",
		settings: "Settings"
	};

	const title = $derived(SCREEN_TITLES[storeNavigation.current] ?? "");

	const readyCount = $derived(
		storeLobby.current
			? (storeLobby.current.members as Array<{ is_ready?: boolean }>).filter(
					(m) => m.is_ready
				).length
			: 0
	);
	const memberCount = $derived(storeLobby.current?.members.length ?? 0);

	function openLobby() {
		storeNavigation.goto("lobby");
	}

	function openProfile() {
		storeNavigation.goto("profile");
	}

	let { content }: { content?: Snippet } = $props();
</script>

<header class="shell-topbar">
	<h1 class="shell-topbar-title">{title}</h1>

	{#if content}
		<div class="shell-topbar-content">
			{@render content()}
		</div>
	{/if}

	<div class="shell-topbar-right">
		{#if storeLobby.isInLobby}
			<button class="shell-topbar-chip" onclick={openLobby}>
				<i class="hn pix hn-diamond text-sm" aria-hidden="true"></i>
				{readyCount}/{memberCount}
			</button>
		{/if}

		<button class="shell-topbar-identity" onclick={openProfile}>
			<Avatar src={storeAuth.avatar} size={32} />
			<TextEffects
				text={storeAuth.username}
				effect="undulate"
				class="font-tiny text-accent"
				amplitude={6}
				speed={2}
				frequency={0.15}
			/>
		</button>
	</div>
</header>

<style>
	.shell-topbar {
		position: relative;
		z-index: 10;
		display: flex;
		align-items: center;
		justify-content: space-between;
		gap: var(--space-3);
		min-height: 52px;
		padding: var(--space-2) var(--space-4);
		background: var(--bg);
		border-bottom: 2px solid var(--border);
	}
	.shell-topbar-title {
		font-family: var(--heading);
		font-size: var(--text-title);
		margin: 0;
	}
	.shell-topbar-content {
		flex: 1;
		min-width: 0;
		display: flex;
		justify-content: center;
	}
	.shell-topbar-right {
		display: flex;
		align-items: center;
		gap: var(--space-3);
	}
	.shell-topbar-chip,
	.shell-topbar-identity {
		display: flex;
		align-items: center;
		gap: var(--space-2);
		min-height: 44px;
		padding: 0 var(--space-2);
		background: transparent;
		border: none;
		color: var(--text-h);
		font-family: var(--tiny);
		font-size: var(--text-small);
		cursor: pointer;
	}
</style>
