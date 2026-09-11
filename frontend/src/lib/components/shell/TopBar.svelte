<script lang="ts">
	import { storeNavigation } from "$stores/navigation.svelte";
	import { storeAuth } from "$stores/auth.svelte";
	import { storeLobby } from "$stores/lobby.svelte";
	import { storeI18n } from "$stores/i18n.svelte";
	import Avatar from "$components/common/Avatar.svelte";
	import TextEffects from "$components/common/TextEffects.svelte";
	import { acceleratorKey } from "$lib/actions/keyboardAccelerators";
	import { storeTopbarContent } from "$stores/topbarContent.svelte";
	import * as m from "$lib/paraglide/messages.js";

	const SCREEN_TITLES: Partial<Record<string, () => string>> = {
		main: () => m.main_title({}, { locale: storeI18n.locale }),
		lobbies: () => m.browse_title({}, { locale: storeI18n.locale }),
		lobby: () => m.lobby_title({}, { locale: storeI18n.locale }),
		decks: () => m.decks_title({}, { locale: storeI18n.locale }),
		shop: () => m.shop_title({}, { locale: storeI18n.locale }),
		profile: () => m.profile_title({}, { locale: storeI18n.locale }),
		stats: () => m.leaderboard_title({}, { locale: storeI18n.locale }),
		settings: () => m.settings_title({}, { locale: storeI18n.locale })
	};

	const title = $derived(SCREEN_TITLES[storeNavigation.current]?.() ?? "");

	const readyCount = $derived(
		storeLobby.current
			? (storeLobby.current.members as Array<{ is_ready?: boolean }>).filter((m) => m.is_ready)
					.length
			: 0
	);
	const memberCount = $derived(storeLobby.current?.members.length ?? 0);

	function openLobby() {
		storeNavigation.goto("lobby");
	}

	function openProfile() {
		storeNavigation.goto("profile");
	}
</script>

<header class="shell-topbar">
	<div class="shell-topbar-left">
		<h1 class="shell-topbar-title">{title}</h1>
	</div>

	{#if storeTopbarContent.current}
		<div class="shell-topbar-content">
			{@render storeTopbarContent.current()}
		</div>
	{/if}

	<div class="shell-topbar-right">
		{#if storeLobby.isInLobby}
			<button
				class="shell-topbar-chip"
				onclick={openLobby}
				title={m.lobby_topbar_open_tooltip({}, { locale: storeI18n.locale })}
			>
				<i class="pia pixelart-icons-font-users text-sm" aria-hidden="true"></i>
				<span class="opacity-60">{readyCount}/{memberCount}</span>
				<i class="pia pixelart-icons-font-check text-sm" aria-hidden="true"></i>
			</button>
			<button
				class="shell-topbar-exit"
				onclick={() => storeLobby.leave()}
				title={m.lobby_exit_tooltip({}, { locale: storeI18n.locale })}
				aria-label={m.lobby_exit_tooltip({}, { locale: storeI18n.locale })}
			>
				<i class="pia pixelart-icons-font-logout text-sm" aria-hidden="true"></i>
			</button>
		{/if}
		<button
			class="shell-topbar-identity"
			onclick={openProfile}
			aria-keyshortcuts={acceleratorKey("profile")}
		>
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
	.shell-topbar-left {
		display: flex;
		align-items: center;
		gap: var(--space-2);
		min-width: 0;
	}
	.shell-topbar-title {
		font-family: var(--heading);
		font-size: var(--text-title);
		margin: 0;
	}
	.shell-topbar-exit {
		display: flex;
		align-items: center;
		min-height: 44px;
		padding: 0 var(--space-2);
		background: transparent;
		border: none;
		color: var(--danger);
		cursor: pointer;
	}
	.shell-topbar-exit:hover {
		filter: brightness(1.25);
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
