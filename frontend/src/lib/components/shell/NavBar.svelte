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
			icon: "pixelart-icons-font-home",
			color: "var(--accent)",
			key: acceleratorKey("home")
		},
		{
			screen: "lobbies",
			label: m.nav_browse({}, { locale: storeI18n.locale }),
			icon: "pixelart-icons-font-search",
			color: "var(--blueCard)",
			key: acceleratorKey("browse")
		},
		{
			screen: "decks",
			label: m.nav_decks({}, { locale: storeI18n.locale }),
			icon: "pixelart-icons-font-grid-3x3",
			color: "var(--greenCard)",
			key: acceleratorKey("decks")
		},
		{
			screen: "shop",
			label: m.nav_shop({}, { locale: storeI18n.locale }),
			icon: "pixelart-icons-font-credit-card",
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

	const COLLAPSE_STORAGE_KEY = "uni:navbar-collapsed";
	let collapsed = $state(
		typeof localStorage !== "undefined" && localStorage.getItem(COLLAPSE_STORAGE_KEY) === "1"
	);

	function toggleCollapsed() {
		collapsed = !collapsed;
		localStorage.setItem(COLLAPSE_STORAGE_KEY, collapsed ? "1" : "0");
	}

	//@ts-ignore
	declare const __APP_VERSION__: string;
</script>

<nav aria-label="Primary" class="shell-navbar" class:shell-navbar-collapsed={collapsed}>
	<button
		type="button"
		class="shell-navbar-collapse-toggle text-2xl"
		onclick={toggleCollapsed}
		title={collapsed
			? m.nav_expand_sidebar({}, { locale: storeI18n.locale })
			: m.nav_collapse_sidebar({}, { locale: storeI18n.locale })}
		aria-label={collapsed
			? m.nav_expand_sidebar({}, { locale: storeI18n.locale })
			: m.nav_collapse_sidebar({}, { locale: storeI18n.locale })}
	>
		<i
			class="pia {collapsed
				? 'pixelart-icons-font-chevron-right-2'
				: 'pixelart-icons-font-chevron-left-2'} leading-none"
			aria-hidden="true"
		></i>
	</button>

	{#each DESTINATIONS as dest (dest.screen)}
		<a
			href={pathForScreen(dest.screen)}
			class="shell-navbar-item"
			style="--nav-item-color: {dest.color}"
			aria-current={storeNavigation.current === dest.screen ? "page" : undefined}
			aria-keyshortcuts={dest.key}
			title={collapsed ? dest.label : undefined}
			onclick={(e) => go(e, dest.screen)}
		>
			<i class="pia {dest.icon} text-xl leading-none" aria-hidden="true"></i>
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
		title={collapsed ? m.nav_menu({}, { locale: storeI18n.locale }) : undefined}
		onclick={openMenu}
	>
		<i class="pia pixelart-icons-font-menu text-xl leading-none" aria-hidden="true"></i>
		<span class="shell-navbar-label">{m.nav_menu({}, { locale: storeI18n.locale })}</span>
		<span class="shell-navbar-key" aria-hidden="true">{acceleratorKey("menu")}</span>
	</a>

	<!-- Rail-only footer: dead-centered at the sidebar's own bottom edge (see
	     .shell-navbar-version below), replacing the old viewport-fixed badge
	     that used to overlap this same corner independently of the rail. -->
	<span class="shell-navbar-version">v{__APP_VERSION__}</span>
</nav>

<style>
	/* Off the rail (mobile bottom bar): toggle plays no role, never rendered.
	   Declared before the desktop media query below so that query's
	   `display: flex` can win at ≥768px without fighting source order. */
	.shell-navbar-collapse-toggle {
		display: none;
	}

	/* Rail-only, same as the toggle above: hidden off the rail (mobile
	   portrait's bottom-bar layout) so it doesn't fall back to a plain
	   inline span in-flow with the nav items. */
	.shell-navbar-version {
		display: none;
	}

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
		/* Neutral gray by default; active destination and hover state get their
		   assigned accent colour with a 12fps discrete stepped transition. */
		color: var(--text);
		text-decoration: none;
		position: relative;
		/* 12fps discrete steps: 0.25s * 12fps = 3 steps */
		transition: color 0.25s steps(3, end);
	}
	.shell-navbar-item:hover,
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
		font-size: 0.7em;
		color: var(--text);
		opacity: 0.6;
		/* Right-anchored so every row's digit lines up at the same edge
		   regardless of how long that row's own label is, rather than sitting
		   right after the label at a different x-position on every row. */
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
			/* Content-sized (as before the collapse feature), just capped so
			   `max-width` has a numeric value to transition from/to — actual
			   width stays governed by the longest label unless that cap bites,
			   which only happens in the collapsed state below. */
			max-width: 16rem;
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

		/* Rail-only: pinned to the sidebar's own top-right corner, out of the
		   destination list's flex flow entirely (not just first in it) —
		   `.shell-navbar` is its `position: relative` anchor. The list gets a
		   matching padding-top so it clears this instead of sitting under it.
		   Hidden entirely on the bottom-nav/phone layout above (collapsing a
		   horizontal bar into icons buys nothing there). */
		.shell-navbar {
			padding-top: 44px;
			padding-bottom: 28px;
			padding-left: var(--space-2);
			padding-right: var(--space-2);
		}
		.shell-navbar-collapse-toggle {
			position: absolute;
			top: var(--space-2);
			right: var(--space-2);
			display: flex;
			align-items: center;
			justify-content: center;
			width: 36px;
			height: 36px;
			padding: 0;
			background: transparent;
			border: none;
			color: var(--text);
			cursor: pointer;
		}
		.shell-navbar-collapse-toggle:hover {
			color: var(--text-h);
		}
		/* The chevron glyph's side-bearing in pixelart-icons-font isn't
		   perfectly symmetric, so dead-centering the button leaves the glyph
		   itself reading slightly left-of-center. Nudged back visually. */
		.shell-navbar-collapse-toggle .pia {
			transform: translateX(1px);
		}

		/* Version footer: dead-centered at the rail's own bottom edge
		   regardless of width, so it doesn't need its own collapsed-state
		   override the way the labels/keys below do. */
		.shell-navbar-version {
			display: block;
			position: absolute;
			bottom: var(--space-2);
			left: 50%;
			transform: translateX(-50%);
			font-family: var(--pypx);
			font-weight: 800;
			font-size: 0.65rem;
			color: #888;
			pointer-events: none;
			user-select: none;
			white-space: nowrap;
		}

		.shell-navbar-label,
		.shell-navbar-key {
			overflow: hidden;
			white-space: nowrap;
			opacity: 1;
		}
		.shell-navbar-label {
			max-width: 8rem;
		}
		.shell-navbar-key {
			max-width: 2rem;
		}

		.shell-navbar-collapsed {
			max-width: 64px;
			padding-left: var(--space-3);
			padding-right: var(--space-3);
		}
		.shell-navbar-collapsed .shell-navbar-item {
			justify-content: center;
			/* Zeroed, not just the label/key's own max-width: `gap` still
			   reserves space after the icon for each now-empty sibling,
			   which visually shifts the icon left of true center. */
			gap: 0;
			padding: var(--space-3) var(--space-2);
		}
		/* Expand toggle only: dead-centered above the icon column instead of
		   pinned top-right, since top-right reads as off-balance once the
		   rail narrows to icon width. The collapse toggle (uncollapsed state)
		   keeps its top-right position untouched. */
		.shell-navbar-collapsed .shell-navbar-collapse-toggle {
			right: auto;
			left: 50%;
			transform: translateX(-50%);
		}
		.shell-navbar-collapsed .shell-navbar-label,
		.shell-navbar-collapsed .shell-navbar-key {
			max-width: 0;
			opacity: 0;
			margin: 0;
		}
	}

	/* Collapse/expand animation, added only when the user hasn't asked for
	   reduced motion — the width/label transitions layered on as a separate,
	   purely additive query so the reduced-motion case falls straight back to
	   the instant show/hide above rather than needing its own override.

	   Overlapped, not simultaneous, and fully continuous (ease) rather than
	   stepped. Collapsing: text starts fading immediately; the rail only
	   starts narrowing once the text is roughly half-faded (delayed by
	   half the fade duration), rather than both starting at once.
	   Expanding mirrors this: rail starts widening immediately, text only
	   starts fading back in once the rail is roughly half-expanded. Each
	   phase's transition is declared on the selector matching its OWN
	   target state (the state being transitioned TO), since that's the
	   computed value the browser reads the transition timing from — not
	   the state being left. */
	@media (min-width: 768px) and (prefers-reduced-motion: no-preference),
		(max-height: 599px) and (prefers-reduced-motion: no-preference) {
		/* Expanding: rail widens immediately, text fades back in once the
		   rail is half-open. */
		.shell-navbar {
			transition: max-width 0.18s ease 0s;
		}
		.shell-navbar-label,
		.shell-navbar-key {
			transition:
				opacity 0.18s ease 0.09s,
				max-width 0.18s ease 0.09s;
		}
		/* Collapsing: text starts fading immediately, rail only starts
		   narrowing once the text is half-faded. */
		.shell-navbar-collapsed {
			transition: max-width 0.18s ease 0.09s;
		}
		.shell-navbar-collapsed .shell-navbar-label,
		.shell-navbar-collapsed .shell-navbar-key {
			transition:
				opacity 0.18s ease 0s,
				max-width 0.18s ease 0s;
		}
	}
</style>
