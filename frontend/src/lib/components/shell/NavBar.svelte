<script lang="ts">
	import { storeNavigation, pathForScreen } from "$stores/navigation.svelte";
	import type { AppScreen } from "$stores/navigation.svelte";

	interface Destination {
		screen: AppScreen;
		label: string;
		icon: string;
	}

	const DESTINATIONS: Destination[] = [
		{ screen: "main", label: "Home", icon: "hn-home" },
		{ screen: "lobbies", label: "Browse", icon: "hn-search" },
		{ screen: "decks", label: "Decks", icon: "hn-viewblocks" },
		{ screen: "shop", label: "Shop", icon: "hn-credit-card" }
	];

	function go(event: MouseEvent, screen: AppScreen) {
		event.preventDefault();
		storeNavigation.goto(screen);
	}

	function openMenu(event: MouseEvent) {
		event.preventDefault();
		storeNavigation.openSettings();
	}
</script>

<nav aria-label="Primary" class="shell-navbar">
	{#each DESTINATIONS as dest (dest.screen)}
		<a
			href={pathForScreen(dest.screen)}
			class="shell-navbar-item"
			aria-current={storeNavigation.current === dest.screen ? "page" : undefined}
			onclick={(e) => go(e, dest.screen)}
		>
			<i class="hn pix {dest.icon} text-xl leading-none" aria-hidden="true"></i>
			<span class="shell-navbar-label">{dest.label}</span>
		</a>
	{/each}
	<a href="#menu" class="shell-navbar-item" onclick={openMenu}>
		<i class="hn pix hn-menu text-xl leading-none" aria-hidden="true"></i>
		<span class="shell-navbar-label">Menu</span>
	</a>
</nav>

<style>
	.shell-navbar {
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
		color: var(--text);
		text-decoration: none;
	}
	.shell-navbar-item[aria-current="page"] {
		color: var(--accent);
	}
	.shell-navbar-label {
		font-family: var(--tiny);
		font-size: var(--text-small);
	}

	@media (min-width: 768px), (max-height: 599px) {
		.shell-navbar {
			flex-direction: column;
			border-top: none;
			border-right: 2px solid var(--border);
			height: 100%;
		}
		.shell-navbar-item {
			flex: none;
			flex-direction: row;
			gap: var(--space-3);
			justify-content: flex-start;
			padding: var(--space-3) var(--space-4);
		}
	}
</style>
