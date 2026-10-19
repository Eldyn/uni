<!-- One item on the shop stage: art on a stage tile, the item name, and either
     the price or an OWNED ribbon, all inside a rarity frame. A click or Enter
     asks the screen to open the item's detail panel. -->
<script module lang="ts">
	import * as messages from "$lib/paraglide/messages.js";
	import type { CatalogueItem } from "$lib/avatar/types";

	/** The paraglide key that names a catalogue item: `uni:eyes_dot` -> `shop_item_uni_eyes_dot`. */
	export function itemLabelKey(id: string): string {
		return `shop_item_${id.replaceAll(":", "_")}`;
	}

	/**
	 * Resolve a shop message key, falling back to the English default when the
	 * key does not exist yet (the i18n catalogue lands after the components).
	 */
	export function shopItemText(key: string, fallback: string, locale: string): string {
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
	import Avatar from "$components/common/Avatar.svelte";
	import RarityFrame from "$components/shop/RarityFrame.svelte";
	import { itemPreviewLoadout } from "$lib/avatar/previewLoadout";
	import type { Loadout } from "$lib/avatar/types";
	import { storeI18n } from "$stores/i18n.svelte";

	let {
		item,
		owned,
		selected,
		loadout,
		onselect
	}: {
		item: CatalogueItem;
		owned: boolean;
		selected: boolean;
		/** The viewer's saved look: its shape and skin dress the preview. */
		loadout: Loadout;
		onselect: () => void;
	} = $props();

	const locale = $derived(storeI18n.locale);
	const name = $derived(shopItemText(itemLabelKey(item.id), item.id, locale));
	const ownedLabel = $derived(shopItemText("shop_owned", "OWNED", locale));
	const preview = $derived(itemPreviewLoadout(item, loadout, owned));

	function activate(event: KeyboardEvent): void {
		if (event.key !== "Enter") return;
		// Stop the browser's own Enter-to-click so onselect fires exactly once.
		event.preventDefault();
		onselect();
	}
</script>

<button
	type="button"
	class="item-card"
	data-item-id={item.id}
	data-selected={selected ? "true" : "false"}
	tabindex={selected ? 0 : -1}
	onclick={onselect}
	onkeydown={activate}
>
	<RarityFrame rarity={item.rarity}>
		<span class="item-art-tile pixel-bordered">
			<span class="item-art" aria-hidden="true"><Avatar loadout={preview} size={96} /></span>
		</span>
		<span class="item-name font-tiny">{name}</span>
		{#if owned}
			<span class="item-owned font-tiny">{ownedLabel}</span>
		{:else}
			<span class="item-price font-pixel">
				<span class="coin" aria-hidden="true"></span>{item.price}
			</span>
		{/if}
	</RarityFrame>
</button>

<style>
	.item-card {
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

	/* A rarity-tinted diagonal stripe behind the whole card, so a card of any
	   rarity reads at a glance while the name and price stay on a surface they
	   remain legible on. */
	.item-card :global(.rarity-frame) {
		background: repeating-linear-gradient(
			135deg,
			color-mix(in srgb, var(--rarity-colour) 22%, var(--surface-deep)) 0 9px,
			color-mix(in srgb, var(--rarity-colour) 8%, var(--surface-deep)) 9px 18px
		);
		transition:
			transform 0.12s ease,
			box-shadow 0.12s ease,
			filter 0.12s ease;
	}

	.item-card:hover :global(.rarity-frame),
	.item-card:focus-visible :global(.rarity-frame) {
		filter: brightness(1.12);
		transform: translateY(-2px);
		box-shadow: 0 0 14px color-mix(in srgb, var(--rarity-colour) 65%, transparent);
	}

	.item-art-tile {
		display: grid;
		place-items: center;
		width: 100%;
		aspect-ratio: 1;
		border-radius: 0;
		--pc-fill: var(--surface);
	}

	.item-art {
		display: block;
		width: 96px;
		height: 96px;
	}

	.item-name {
		/* A very long or pseudo-localised name wraps instead of widening the card. */
		min-width: 0;
		padding: 0.35rem 0.4rem 0;
		overflow-wrap: anywhere;
		text-align: center;
		line-height: 1.15;
		color: var(--text-h);
	}

	.item-price,
	.item-owned {
		display: flex;
		align-items: center;
		justify-content: center;
		gap: 0.3rem;
		padding: 0.35rem 0.4rem 0.5rem;
		font-size: 0.85rem;
	}

	.item-price {
		color: var(--gold);
	}

	.item-owned {
		color: var(--accent);
		letter-spacing: 0.06em;
	}

	.coin {
		display: inline-block;
		width: 0.6em;
		height: 0.6em;
		border-radius: 50%;
		background: var(--gold);
	}
</style>
