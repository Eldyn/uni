<script lang="ts">
	import { storeNavigation, pathForScreen } from "$stores/navigation.svelte";
	import type { AppScreen } from "$stores/navigation.svelte";
	import { ACCELERATOR_KEYS } from "$lib/actions/keyboardAccelerators";

	interface Destination {
		screen: AppScreen;
		label: string;
		icon: string;
		/** Permanent per-item color (CSS custom property value), independent of active state. */
		color: string;
		key: string;
	}

	const DESTINATIONS: Destination[] = [
		{ screen: "main", label: "Home", icon: "hn-home", color: "var(--accent)", key: ACCELERATOR_KEYS.home },
		{
			screen: "lobbies",
			label: "Browse",
			icon: "hn-search",
			color: "var(--blueCard)",
			key: ACCELERATOR_KEYS.browse
		},
		{
			screen: "decks",
			label: "Decks",
			icon: "hn-viewblocks",
			color: "var(--greenCard)",
			key: ACCELERATOR_KEYS.decks
		},
		{ screen: "shop", label: "Shop", icon: "hn-credit-card", color: "var(--yellowCard)", key: ACCELERATOR_KEYS.shop }
	];

	function go(event: MouseEvent, screen: AppScreen) {
		event.preventDefault();
		storeNavigation.goto(screen);
	}

	function openMenu(event: MouseEvent) {
		event.preventDefault();
		storeNavigation.goto("settings");
	}
</script>

{#snippet mnemonicLabel(label: string, key: string)}
	{#if label.toUpperCase().startsWith(key)}
		<span class="mnemonic-key">{label[0]}</span>{label.slice(1)}
	{:else}
		{label}
	{/if}
{/snippet}

<nav aria-label="Primary" class="shell-navbar">
	{#each DESTINATIONS as dest (dest.screen)}
		<a
			href={pathForScreen(dest.screen)}
			class="shell-navbar-item"
			style="--nav-item-color: {dest.color}"
			aria-current={storeNavigation.current === dest.screen ? "page" : undefined}
			aria-keyshortcuts={dest.key}
			onclick={(e) => go(e, dest.screen)}
		>
			<i class="hn pix {dest.icon} text-xl leading-none" aria-hidden="true"></i>
			<span class="shell-navbar-label">{@render mnemonicLabel(dest.label, dest.key)}</span>
		</a>
	{/each}
	<a
		href={pathForScreen("settings")}
		class="shell-navbar-item"
		style="--nav-item-color: var(--accent)"
		aria-current={storeNavigation.current === "settings" ? "page" : undefined}
		aria-keyshortcuts={ACCELERATOR_KEYS.menu}
		onclick={openMenu}
	>
		<i class="hn pix hn-menu text-xl leading-none" aria-hidden="true"></i>
		<span class="shell-navbar-label">{@render mnemonicLabel("Menu", ACCELERATOR_KEYS.menu)}</span>
	</a>
</nav>

<style>
	.shell-navbar {
		position: relative;
		z-index: 10;
		display: flex;
		background: var(--bg);
		border-top: 2px solid var(--border);
	}
	.shell-navbar-item {
		flex: 1;
		display: flex;
		flex-direction: column;
		align-items: center;
		justify-content: center;
		gap: var(--space-1);
		min-height: 44px;
		padding: var(--space-2) 0;
		color: var(--nav-item-color, var(--text));
		text-decoration: none;
		position: relative;
	}
	.shell-navbar-item[aria-current="page"]::after {
		content: "";
		position: absolute;
		inset: 0 var(--space-2) auto var(--space-2);
		top: 0;
		height: 3px;
		background: var(--nav-item-color, var(--accent));
	}
	.shell-navbar-label {
		font-family: var(--tiny);
		font-size: var(--text-small);
	}

	.mnemonic-key {
		text-decoration: none;
	}

	@media (min-width: 768px), (max-height: 599px) {
		.mnemonic-key {
			text-decoration: underline;
			text-decoration-thickness: 2px;
			text-underline-offset: 2px;
		}
	}

	@media (min-width: 768px), (max-height: 599px) {
		.shell-navbar {
			flex-direction: column;
			align-items: center;
			justify-content: center;
			border-top: none;
			border-right: 2px solid var(--border);
			height: 100%;
		}
		.shell-navbar-item {
			flex: none;
			width: 100%;
			flex-direction: row;
			gap: var(--space-3);
			justify-content: flex-start;
			padding: var(--space-3) var(--space-4);
		}
		.shell-navbar-item[aria-current="page"]::after {
			inset: var(--space-2) auto var(--space-2) 0;
			top: auto;
			left: 0;
			width: 3px;
			height: auto;
		}
	}
</style>
