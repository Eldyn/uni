<script lang="ts">
	import { storeNavigation, pathForScreen } from "$stores/navigation.svelte";
	import type { AppScreen } from "$stores/navigation.svelte";
	import { acceleratorKey } from "$lib/actions/keyboardAccelerators";
	import { storeI18n } from "$stores/i18n.svelte";
	import * as m from "$lib/paraglide/messages.js";

	interface Destination {
		screen: AppScreen;
		label: string;
		icon: string;
		/** Permanent per-item color (CSS custom property value), independent of active state. */
		color: string;
		key: string;
	}

	const DESTINATIONS = $derived<Destination[]>([
		{
			screen: "main",
			label: m.nav_home({}, { locale: storeI18n.locale }),
			icon: "hn-home",
			color: "var(--accent)",
			key: acceleratorKey("home")
		},
		{
			screen: "lobbies",
			label: m.nav_browse({}, { locale: storeI18n.locale }),
			icon: "hn-search",
			color: "var(--blueCard)",
			key: acceleratorKey("browse")
		},
		{
			screen: "decks",
			label: m.nav_decks({}, { locale: storeI18n.locale }),
			icon: "hn-viewblocks",
			color: "var(--greenCard)",
			key: acceleratorKey("decks")
		},
		{
			screen: "shop",
			label: m.nav_shop({}, { locale: storeI18n.locale }),
			icon: "hn-credit-card",
			color: "var(--yellowCard)",
			key: acceleratorKey("shop")
		}
	]);

	function go(event: MouseEvent, screen: AppScreen) {
		event.preventDefault();
		storeNavigation.goto(screen);
	}

	function openMenu(event: MouseEvent) {
		event.preventDefault();
		storeNavigation.goto("settings");
	}
</script>

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
			<span class="shell-navbar-label">{dest.label}</span>
			<span class="shell-navbar-key" aria-hidden="true">{dest.key}</span>
		</a>
	{/each}
	<a
		href={pathForScreen("settings")}
		class="shell-navbar-item"
		style="--nav-item-color: var(--accent)"
		aria-current={storeNavigation.current === "settings" ? "page" : undefined}
		aria-keyshortcuts={acceleratorKey("menu")}
		onclick={openMenu}
	>
		<i class="hn pix hn-menu text-xl leading-none" aria-hidden="true"></i>
		<span class="shell-navbar-label">{m.nav_menu({}, { locale: storeI18n.locale })}</span>
		<span class="shell-navbar-key" aria-hidden="true">{acceleratorKey("menu")}</span>
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
		/* Neutral gray by default; only the active destination gets its
		   assigned accent colour (see the [aria-current="page"] rule below). */
		color: var(--text);
		text-decoration: none;
		position: relative;
	}
	.shell-navbar-item[aria-current="page"] {
		color: var(--nav-item-color, var(--accent));
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

	.shell-navbar-key {
		display: none;
		font-family: var(--tiny);
		font-size: var(--text-small);
		color: var(--text);
		opacity: 0.6;
		margin-left: auto;
	}

	@media (min-width: 768px), (max-height: 599px) {
		.shell-navbar-key {
			display: inline;
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
