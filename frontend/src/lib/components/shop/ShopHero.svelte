<!-- A marketplace hero tile: the item name top centre in Pypx thick, the art
     behind it, and the price bottom right with the Blank icon. A set or
     collection prefers its wide hero image and falls back to the baked avatar
     preview when the file is missing. The large hero also carries BUY / TRY ON. -->
<script lang="ts">
	import AvatarSprite from "$components/common/AvatarSprite.svelte";
	import BlankIcon from "$components/common/BlankIcon.svelte";
	import RarityFrame from "$components/shop/RarityFrame.svelte";
	import { itemLabelKey, shopItemText } from "$components/shop/ItemCard.svelte";
	import { createBakedAvatar } from "$lib/avatar/useBakedAvatar.svelte";
	import { assetUrl } from "$lib/utils/assetUrl";
	import type { CatalogueItem, Loadout } from "$lib/avatar/types";
	import { storeI18n } from "$stores/i18n.svelte";

	let {
		item,
		owned,
		balance,
		guest = false,
		loadout,
		onbuy,
		ontry,
		onselect,
		size = "small"
	}: {
		item: CatalogueItem;
		owned: boolean;
		balance: number;
		guest?: boolean;
		/** The preview look to bake behind the tile. */
		loadout: Loadout | null;
		onbuy: () => void;
		ontry: () => void;
		onselect?: () => void;
		size?: "large" | "small";
	} = $props();

	const baked = createBakedAvatar();

	$effect(() => {
		baked.set(loadout);
	});

	$effect(() => {
		return () => baked.destroy();
	});

	const locale = $derived(storeI18n.locale);
	const currencyName = $derived(shopItemText("shop_currency_name", "Blank", locale));
	const name = $derived(shopItemText(itemLabelKey(item.id), item.id, locale));
	const ownedLabel = $derived(shopItemText("shop_owned", "OWNED", locale));
	const buyLabel = $derived(
		guest
			? shopItemText("shop_register_to_buy", "Register to buy", locale)
			: shopItemText("shop_buy", "BUY", locale)
	);
	const tryLabel = $derived(shopItemText("shop_try_on", "TRY ON", locale));

	const isCollection = $derived(
		item.slot === "set" ||
			item.slot === "card_face" ||
			item.slot === "card_back" ||
			item.slot === "playmat"
	);
	const heroPath = $derived(`/assets/shop/heroes/${item.id.replaceAll(":", "_")}.png`);
	/** The item whose hero image failed to load; it falls back to the avatar preview. */
	let failedId = $state<string | null>(null);
	const imageFailed = $derived(failedId === item.id);

	const buyDisabled = $derived(guest || owned || balance < item.price);
	const avatarSize = $derived(size === "large" ? 192 : 96);
</script>

<article class="shop-hero" data-hero-id={item.id} data-featured-id={item.id} data-size={size}>
	<RarityFrame rarity={item.rarity} class="hero-frame">
		<span class="hero-art" aria-hidden="true">
			{#if isCollection && !imageFailed}
				<img
					class="hero-image"
					src={assetUrl(heroPath)}
					alt=""
					onerror={() => (failedId = item.id)}
				/>
			{:else if baked.url}
				<AvatarSprite url={baked.url} size={avatarSize} />
			{:else}
				<!-- The bake is not ready yet: an empty stage keeps the tile the same size. -->
				<span class="hero-art-empty"></span>
			{/if}
		</span>

		<span class="hero-name">{name}</span>

		{#if owned}
			<span class="hero-owned">{ownedLabel}</span>
		{:else}
			<span class="hero-price"><BlankIcon label={currencyName} />{item.price}</span>
		{/if}

		{#if size === "large"}
			<span class="hero-actions">
				<button
					type="button"
					class="btn"
					disabled={buyDisabled}
					onclick={() => {
						if (!buyDisabled) onbuy();
					}}
				>
					{buyLabel}
				</button>
				<button type="button" class="btn-secondary" onclick={ontry}>{tryLabel}</button>
			</span>
		{/if}

		{#if onselect}
			<button type="button" class="hero-open" aria-label={name} onclick={onselect}></button>
		{/if}
	</RarityFrame>
</article>

<style>
	.shop-hero {
		position: relative;
		display: flex;
		min-width: 0;
		min-height: 0;
	}

	.shop-hero[data-size="large"] {
		min-height: clamp(14rem, 34vh, 22rem);
	}

	.shop-hero[data-size="small"] {
		min-height: 6rem;
	}

	.shop-hero :global(.hero-frame) {
		flex: 1;
		background: var(--surface-deep);
	}

	.hero-art {
		position: absolute;
		inset: 0;
		display: grid;
		place-items: center;
		overflow: hidden;
	}

	.hero-image {
		width: 100%;
		height: 100%;
		object-fit: cover;
		image-rendering: pixelated;
	}

	.hero-art-empty {
		display: block;
		width: 96px;
		height: 96px;
	}

	.hero-name {
		position: absolute;
		top: 0.35rem;
		left: 0;
		right: 0;
		z-index: 2;
		margin: 0;
		padding: 0 0.5rem;
		font-family: var(--pypx);
		font-weight: 800;
		line-height: 1.2;
		letter-spacing: 0.04em;
		text-align: center;
		text-transform: uppercase;
		overflow-wrap: anywhere;
		pointer-events: none;
		color: var(--text-h);
	}

	.hero-price {
		position: absolute;
		right: 0;
		bottom: 0;
		display: flex;
		align-items: center;
		gap: 0.3rem;
		padding: 0.35rem 0.5rem;
		font-family: var(--pypx);
		font-weight: 800;
		pointer-events: none;
		color: var(--accent);
	}

	.hero-owned {
		position: absolute;
		left: 0;
		bottom: 0;
		display: flex;
		align-items: center;
		gap: 0.3rem;
		padding: 0.35rem 0.5rem;
		font-family: var(--pypx);
		font-weight: 800;
		letter-spacing: 0.06em;
		pointer-events: none;
		color: var(--accent);
	}

	.hero-actions {
		position: absolute;
		bottom: 0;
		left: 0;
		z-index: 4;
		display: flex;
		gap: 0.5rem;
		padding: 0.5rem;
	}

	.hero-actions button:disabled {
		opacity: 0.5;
		cursor: not-allowed;
	}

	.hero-open {
		position: absolute;
		inset: 0;
		z-index: 3;
		padding: 0;
		border: none;
		background: transparent;
		cursor: pointer;
		clip-path: none;
	}

	.hero-open:focus-visible {
		outline: 2px solid var(--accent);
		outline-offset: -4px;
	}
</style>
