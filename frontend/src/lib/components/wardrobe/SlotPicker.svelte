<!-- The wardrobe's item grid for one slot: owned items on rarity frames plus a
     "none" tile where the slot is optional. It is a roving grid: the arrows,
     Home and End move the single tab stop, and a click or Enter picks a tile.
     When a worn set covers the slot the tiles are disabled and a note shows. -->
<script module lang="ts">
	import * as messages from "$lib/paraglide/messages.js";

	/**
	 * Resolve a wardrobe message key, falling back to the English default when
	 * the key does not exist yet (the i18n catalogue lands after the components).
	 */
	export function wardrobeText(key: string, fallback: string, locale: string): string {
		const translate = (messages as Record<string, unknown>)[key];
		if (typeof translate !== "function") return fallback;
		const value = (translate as (inputs: object, options: { locale: string }) => unknown)(
			{},
			{ locale }
		);
		return typeof value === "string" && value.length > 0 ? value : fallback;
	}
</script>

<script lang="ts">
	import AvatarFit from "$components/common/AvatarFit.svelte";
	import RarityFrame from "$components/shop/RarityFrame.svelte";
	import { itemLabelKey, shopItemText } from "$components/shop/ItemCard.svelte";
	import { itemPreviewLoadout } from "$lib/avatar/previewLoadout";
	import { storeI18n } from "$stores/i18n.svelte";
	import type { CatalogueItem, Loadout } from "$lib/avatar/types";

	let {
		items,
		equipped,
		blocked,
		allowNone,
		noneLabel,
		blockNote,
		label,
		loadout,
		onselect,
		onnone
	}: {
		items: CatalogueItem[];
		equipped: string | null;
		blocked: boolean;
		allowNone: boolean;
		noneLabel: string;
		blockNote: string;
		label: string;
		/** The draft look: its shape and skin dress each preview. */
		loadout: Loadout;
		onselect: (item: CatalogueItem) => void;
		onnone: () => void;
	} = $props();

	/** Cards per row; the up and down arrows step a row. */
	const COLUMNS = 3;

	const locale = $derived(storeI18n.locale);

	/** Index of the tile that holds the grid's single tab stop. */
	let focused = $state(0);
	let gridEl: HTMLDivElement | undefined = $state();

	function tiles(): HTMLElement[] {
		return [...(gridEl?.querySelectorAll<HTMLElement>("[data-tile]") ?? [])];
	}

	function focusTile(index: number): void {
		const list = tiles();
		if (list.length === 0) return;
		const next = (index + list.length) % list.length;
		focused = next;
		list[next]?.focus();
	}

	function handleFocusIn(event: FocusEvent): void {
		const index = tiles().indexOf(event.target as HTMLElement);
		if (index >= 0) focused = index;
	}

	function handleKeydown(event: KeyboardEvent): void {
		switch (event.key) {
			case "ArrowRight":
				event.preventDefault();
				focusTile(focused + 1);
				break;
			case "ArrowLeft":
				event.preventDefault();
				focusTile(focused - 1);
				break;
			case "ArrowDown":
				event.preventDefault();
				focusTile(focused + COLUMNS);
				break;
			case "ArrowUp":
				event.preventDefault();
				focusTile(focused - COLUMNS);
				break;
			case "Home":
				event.preventDefault();
				focusTile(0);
				break;
			case "End":
				event.preventDefault();
				focusTile(tiles().length - 1);
				break;
		}
	}
</script>

{#if blocked}
	<p class="slot-blocked font-tiny" role="note">{blockNote}</p>
{/if}

<div
	class="slot-grid"
	role="grid"
	tabindex="-1"
	aria-label={label}
	bind:this={gridEl}
	onfocusin={handleFocusIn}
	onkeydown={handleKeydown}
>
	{#if allowNone}
		<button
			type="button"
			class="slot-tile slot-none font-tiny"
			data-tile
			data-none="true"
			disabled={blocked}
			aria-pressed={equipped === null}
			tabindex={focused === 0 ? 0 : -1}
			onclick={onnone}
		>
			{noneLabel}
		</button>
	{/if}

	{#each items as item, index (item.id)}
		{@const tile = allowNone ? index + 1 : index}
		<button
			type="button"
			class="slot-tile"
			data-tile
			data-item-id={item.id}
			disabled={blocked}
			aria-pressed={equipped === item.id}
			tabindex={focused === tile ? 0 : -1}
			onclick={() => onselect(item)}
		>
			<RarityFrame rarity={item.rarity}>
				<span class="slot-art-tile pixel-bordered">
					<span class="slot-art" aria-hidden="true">
						<AvatarFit loadout={itemPreviewLoadout(item, loadout, true)} />
					</span>
				</span>
				<span class="slot-name font-tiny">
					{shopItemText(itemLabelKey(item.id), item.name, locale)}
				</span>
			</RarityFrame>
		</button>
	{/each}
</div>

<style>
	.slot-blocked {
		margin: 0 0 0.5rem;
		color: var(--accent);
		letter-spacing: 0.04em;
	}

	.slot-grid {
		display: grid;
		grid-template-columns: repeat(3, minmax(0, 1fr));
		gap: 0.5rem;
	}

	.slot-tile {
		display: block;
		width: 100%;
		padding: 0;
		border: none;
		background: none;
		color: var(--text);
		cursor: pointer;
		/* A card is a square tile, so it opts out of the global notch clip. */
		clip-path: none;
	}

	.slot-none {
		display: grid;
		place-items: center;
		aspect-ratio: 1;
		border: 2px dashed var(--border);
		background: var(--surface-deep);
		color: var(--text);
	}

	.slot-none[aria-pressed="true"] {
		border-style: solid;
		border-color: var(--accent);
		color: var(--text-h);
	}

	.slot-tile:disabled {
		opacity: 0.4;
		cursor: not-allowed;
	}

	.slot-art-tile {
		display: grid;
		grid-template-columns: minmax(0, 1fr);
		place-items: center;
		width: 100%;
		aspect-ratio: 1;
		overflow: hidden;
		--pc-fill: var(--surface);
	}

	.slot-art {
		display: block;
		width: 100%;
		height: 100%;
	}

	.slot-name {
		display: block;
		min-width: 0;
		padding: 0.35rem 0.4rem 0.5rem;
		overflow-wrap: anywhere;
		text-align: center;
		line-height: 1.15;
		color: var(--text-h);
	}
</style>
