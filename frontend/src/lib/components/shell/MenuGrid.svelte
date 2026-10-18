<script lang="ts">
	/**
	 * @file MenuGrid.svelte
	 * @brief Tile grid opened by the nav Menu item: Leaderboard, Wardrobe
	 * (disabled), Settings. Positioning is left to the host (NavBar).
	 */
	import { onMount } from "svelte";
	import { storeMenu } from "$stores/menu.svelte";
	import { storeNavigation } from "$stores/navigation.svelte";
	import type { AppScreen } from "$stores/navigation.svelte";
	import { storeI18n } from "$stores/i18n.svelte";
	import * as m from "$lib/paraglide/messages.js";

	interface MenuTile {
		label: string;
		icon: string;
		disabled: boolean;
		screen?: AppScreen;
	}

	const TILES = $derived<MenuTile[]>([
		{
			label: m.nav_settings({}, { locale: storeI18n.locale }),
			icon: "pixelart-icons-font-settings-cog",
			disabled: false,
			screen: "settings"
		},
		{
			label: m.nav_wardrobe({}, { locale: storeI18n.locale }),
			icon: "pixelart-icons-font-shirt",
			disabled: true
		},
		{
			label: m.nav_leaderboard({}, { locale: storeI18n.locale }),
			icon: "pixelart-icons-font-trophy",
			disabled: false,
			screen: "stats"
		}
	]);

	const GRID_LABEL = $derived(m.nav_menu_grid_label({}, { locale: storeI18n.locale }));
	const SOON_LABEL = $derived(m.nav_wardrobe_soon({}, { locale: storeI18n.locale }));

	/** Index of the tile that holds the grid's single tab stop. */
	let focused = $state(0);
	let tileEls: HTMLButtonElement[] = $state([]);

	function enabledIndexes(): number[] {
		return TILES.reduce<number[]>((acc, tile, index) => {
			if (!tile.disabled) acc.push(index);
			return acc;
		}, []);
	}

	function focusTile(index: number | undefined): void {
		if (index === undefined || index < 0) return;
		focused = index;
		tileEls[index]?.focus();
	}

	function moveHorizontal(step: number): void {
		const enabled = enabledIndexes();
		if (enabled.length === 0) return;
		const position = enabled.indexOf(focused);
		const next = (position + step + enabled.length) % enabled.length;
		focusTile(enabled[next]);
	}

	/** One row is 3 columns; stay put when the target slot is empty. */
	function moveVertical(offset: number): void {
		const target = focused + offset;
		if (target < 0 || target >= TILES.length || TILES[target].disabled) return;
		focusTile(target);
	}

	function activate(index: number): void {
		const tile = TILES[index];
		if (!tile || tile.disabled) return;
		tileEls[index]?.click();
	}

	function handleKeydown(event: KeyboardEvent): void {
		switch (event.key) {
			case "ArrowRight":
				event.preventDefault();
				moveHorizontal(1);
				break;
			case "ArrowLeft":
				event.preventDefault();
				moveHorizontal(-1);
				break;
			case "ArrowDown":
				event.preventDefault();
				moveVertical(3);
				break;
			case "ArrowUp":
				event.preventDefault();
				moveVertical(-3);
				break;
			case "Home":
				event.preventDefault();
				focusTile(enabledIndexes()[0]);
				break;
			case "End": {
				event.preventDefault();
				const enabled = enabledIndexes();
				focusTile(enabled[enabled.length - 1]);
				break;
			}
			case "Enter":
			case " ": {
				const trigger = (event.target as HTMLElement | null)?.closest<HTMLButtonElement>(
					'[role="menuitem"]'
				);
				if (!trigger) break;
				event.preventDefault();
				activate(tileEls.indexOf(trigger));
				break;
			}
		}
	}

	function go(tile: MenuTile): void {
		if (tile.disabled || !tile.screen) return;
		storeNavigation.goto(tile.screen);
		storeMenu.close();
	}

	onMount(() => {
		focusTile(enabledIndexes()[0]);
	});
</script>

<div class="menu-grid" role="menu" aria-label={GRID_LABEL} tabindex="-1" onkeydown={handleKeydown}>
	{#each TILES as tile, index (tile.label)}
		<button
			type="button"
			role="menuitem"
			class="menu-tile font-tiny"
			aria-disabled={tile.disabled ? "true" : undefined}
			tabindex={index === focused && !tile.disabled ? 0 : -1}
			bind:this={tileEls[index]}
			onfocus={() => (focused = index)}
			onclick={() => go(tile)}
		>
			<i class="pia {tile.icon} text-xl leading-none" aria-hidden="true"></i>
			<span class="menu-tile-label">{tile.label}</span>
			{#if tile.disabled}
				<span class="menu-tile-badge">{SOON_LABEL}</span>
			{/if}
		</button>
	{/each}
</div>

<style>
	/* INFO: each tile is about one nav-bar button: a small filled square. */
	.menu-grid {
		display: grid;
		grid-template-columns: repeat(3, var(--menu-tile-size));
		gap: 0;
		--menu-tile-size: 5rem;
	}

	.menu-tile {
		display: flex;
		flex-direction: column;
		align-items: center;
		justify-content: center;
		gap: var(--space-1);
		width: var(--menu-tile-size);
		aspect-ratio: 1;
		min-width: 0;
		padding: var(--space-1);
		background: var(--surface-deep);
		border: 0;
		border-radius: 0;
		/* INFO: the global button rule notches every corner; tiles are square. */
		clip-path: none;
		outline: none;
		color: var(--text);
		cursor: pointer;
		transition: color 0.12s ease;
	}
	.menu-tile:hover,
	.menu-tile:focus-visible {
		outline: none;
		box-shadow: inset 0 0 0 2px var(--accent);
		color: var(--text-h);
	}
	.menu-tile[aria-disabled="true"] {
		opacity: 0.5;
		cursor: not-allowed;
	}

	.menu-tile-label {
		min-width: 0;
		text-align: center;
		overflow-wrap: break-word;
		line-height: 1.1;
		font-size: 0.65rem;
	}

	.menu-tile-badge {
		padding: 0 var(--space-1);
		color: var(--accent);
		font-size: 0.6rem;
		line-height: 1.1;
	}
</style>
