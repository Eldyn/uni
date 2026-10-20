<!-- One marketplace card: the item name top centre, the art centred below it, the
     wishlist count bottom left and the price bottom right with the Blank icon.
     The whole card opens the detail drawer; the wishlist toggle is a separate
     control in the corner that never opens it. -->
<script lang="ts">
	import AvatarFit from "$components/common/AvatarFit.svelte";
	import BlankIcon from "$components/common/BlankIcon.svelte";
	import RarityFrame from "$components/shop/RarityFrame.svelte";
	import { itemLabelKey, shopItemText } from "$components/shop/ItemCard.svelte";
	import { itemPreviewLoadout } from "$lib/avatar/previewLoadout";
	import type { CatalogueItem, Loadout } from "$lib/avatar/types";
	import { storeI18n } from "$stores/i18n.svelte";

	let {
		item,
		owned,
		selected,
		loadout,
		wishlisted = false,
		wishlistCount = 0,
		showWishlist = true,
		onselect,
		ontoggle
	}: {
		item: CatalogueItem;
		owned: boolean;
		selected: boolean;
		/** The viewer's saved look: its shape and skin dress the preview. */
		loadout: Loadout;
		wishlisted?: boolean;
		wishlistCount?: number;
		/** False for guests: the count is hidden and the toggle is not rendered. */
		showWishlist?: boolean;
		onselect: () => void;
		ontoggle: () => void;
	} = $props();

	const locale = $derived(storeI18n.locale);
	const currencyName = $derived(shopItemText("shop_currency_name", "Blank", locale));
	const name = $derived(shopItemText(itemLabelKey(item.id), item.id, locale));
	const ownedLabel = $derived(shopItemText("shop_owned", "OWNED", locale));
	const addLabel = $derived(shopItemText("shop_wishlist_add", "Add to wishlist", locale));
	const removeLabel = $derived(
		shopItemText("shop_wishlist_remove", "Remove from wishlist", locale)
	);
	const preview = $derived(itemPreviewLoadout(item, loadout, owned));

	function activate(event: KeyboardEvent): void {
		if (event.key !== "Enter") return;
		// Stop the browser's own Enter-to-click so onselect fires exactly once.
		event.preventDefault();
		onselect();
	}

	function toggleWishlist(event: MouseEvent): void {
		// The toggle sits inside the card; the card must stay closed.
		event.stopPropagation();
		ontoggle();
	}
</script>

<article class="marketplace-card" data-marketplace-id={item.id}>
	<button
		type="button"
		class="marketplace-card-open"
		data-item-id={item.id}
		data-selected={selected ? "true" : "false"}
		tabindex={selected ? 0 : -1}
		onclick={onselect}
		onkeydown={activate}
	>
		<RarityFrame rarity={item.rarity}>
			<span class="marketplace-name">{name}</span>
			<span class="marketplace-art-tile pixel-bordered">
				<span class="marketplace-art" aria-hidden="true"><AvatarFit loadout={preview} /></span>
			</span>
			<span class="marketplace-footer">
				{#if showWishlist}
					<span class="marketplace-wishlist-count" data-wishlist-count={wishlistCount}>
						<svg class="marketplace-bookmark" viewBox="0 0 10 10" aria-hidden="true">
							<path d="M2 1h6v8L5 6.5 2 9z" />
						</svg>
						{wishlistCount}
					</span>
				{/if}
				{#if owned}
					<span class="marketplace-owned">{ownedLabel}</span>
				{:else}
					<span class="marketplace-price"><BlankIcon label={currencyName} />{item.price}</span>
				{/if}
			</span>
		</RarityFrame>
	</button>

	{#if showWishlist}
		<button
			type="button"
			class="marketplace-wishlist-toggle"
			data-wishlist-toggle
			aria-pressed={wishlisted}
			aria-label={wishlisted ? removeLabel : addLabel}
			onclick={toggleWishlist}
		>
			<svg class="marketplace-bookmark" viewBox="0 0 10 10" aria-hidden="true">
				<path d="M2 1h6v8L5 6.5 2 9z" />
			</svg>
		</button>
	{/if}
</article>

<style>
	.marketplace-card {
		position: relative;
		display: flex;
		min-width: 0;
	}

	.marketplace-card-open {
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

	.marketplace-card-open :global(.rarity-frame) {
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

	.marketplace-card-open:hover :global(.rarity-frame),
	.marketplace-card-open:focus-visible :global(.rarity-frame) {
		filter: brightness(1.12);
		transform: translateY(-2px);
		box-shadow: 0 0 14px color-mix(in srgb, var(--rarity-colour) 65%, transparent);
	}

	.marketplace-name {
		display: block;
		padding: 0.4rem 2rem 0.15rem;
		font-family: var(--pypx);
		font-weight: 800;
		font-size: 0.8rem;
		line-height: 1.15;
		letter-spacing: 0.04em;
		text-align: center;
		text-transform: uppercase;
		overflow-wrap: anywhere;
		color: var(--text-h);
	}

	.marketplace-art-tile {
		display: grid;
		grid-template-columns: minmax(0, 1fr);
		place-items: center;
		width: 100%;
		aspect-ratio: 1;
		overflow: hidden;
		border-radius: 0;
		--pc-fill: var(--surface);
	}

	.marketplace-art {
		display: block;
		width: 100%;
		height: 100%;
	}

	.marketplace-footer {
		display: flex;
		align-items: center;
		justify-content: space-between;
		gap: 0.4rem;
		padding: 0.35rem 0.45rem 0.5rem;
	}

	.marketplace-wishlist-count {
		display: flex;
		align-items: center;
		gap: 0.25rem;
		font-family: var(--pypx);
		font-weight: 800;
		font-size: 0.75rem;
		color: var(--text);
	}

	.marketplace-bookmark {
		width: 0.75rem;
		height: 0.75rem;
		fill: currentColor;
	}

	.marketplace-price,
	.marketplace-owned {
		display: flex;
		align-items: center;
		gap: 0.3rem;
		margin-left: auto;
		font-family: var(--pypx);
		font-weight: 800;
		font-size: 0.85rem;
		color: var(--accent);
	}

	.marketplace-owned {
		letter-spacing: 0.06em;
	}

	.marketplace-wishlist-toggle {
		position: absolute;
		top: 0.25rem;
		right: 0.25rem;
		z-index: 4;
		display: grid;
		place-items: center;
		width: 1.6rem;
		height: 1.6rem;
		padding: 0;
		border: 1px solid var(--surface-deep);
		background: color-mix(in srgb, var(--surface) 85%, transparent);
		color: var(--text);
		cursor: pointer;
		clip-path: none;
	}

	.marketplace-wishlist-toggle[aria-pressed="true"] {
		border-color: var(--accent);
		color: var(--accent);
	}

	.marketplace-wishlist-toggle:hover,
	.marketplace-wishlist-toggle:focus-visible {
		color: var(--accent);
	}
</style>
