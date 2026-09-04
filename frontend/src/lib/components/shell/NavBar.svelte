<script lang="ts">
	import { storeNavigation, pathForScreen } from "$stores/navigation.svelte";
	import type { AppScreen } from "$stores/navigation.svelte";

	interface Destination {
		screen: AppScreen;
		label: string;
		icon: string;
		/** Permanent per-item color (CSS custom property value), independent of active state. */
		color: string;
	}

	const DESTINATIONS: Destination[] = [
		{ screen: "main", label: "Home", icon: "hn-home", color: "var(--accent)" },
		{ screen: "lobbies", label: "Browse", icon: "hn-search", color: "var(--blueCard)" },
		{ screen: "decks", label: "Decks", icon: "hn-viewblocks", color: "var(--greenCard)" },
		{ screen: "shop", label: "Shop", icon: "hn-credit-card", color: "var(--yellowCard)" }
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
			style="--nav-item-color: {dest.color}"
			aria-current={storeNavigation.current === dest.screen ? "page" : undefined}
			onclick={(e) => go(e, dest.screen)}
		>
			<i class="hn pix {dest.icon} text-xl leading-none" aria-hidden="true"></i>
			<span class="shell-navbar-label">{dest.label}</span>
		</a>
	{/each}
	<a
		href="#menu"
		class="shell-navbar-item"
		style="--nav-item-color: var(--accent)"
		onclick={openMenu}
	>
		<i class="hn pix hn-menu text-xl leading-none" aria-hidden="true"></i>
		<span class="shell-navbar-label">Menu</span>
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
