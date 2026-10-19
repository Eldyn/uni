<!-- The shop's featured hero: a wide card with a slight skew, the live preview
     avatar wearing the item, the name, rarity and price, and the BUY / TRY ON
     actions. It renders nothing when the catalogue has no featured item. -->
<script lang="ts">
	import AvatarSprite from "$components/common/AvatarSprite.svelte";
	import { itemLabelKey, shopItemText } from "$components/shop/ItemCard.svelte";
	import { storeI18n } from "$stores/i18n.svelte";
	import { RARITY_COLOURS, rarityLabelKey } from "$lib/shop/rarity";
	import { createBakedAvatar } from "$lib/avatar/useBakedAvatar.svelte";
	import type { CatalogueItem, Loadout } from "$lib/avatar/types";

	let {
		item,
		owned,
		balance,
		guest,
		loadout,
		onbuy,
		ontry
	}: {
		item: CatalogueItem | null;
		owned: boolean;
		balance: number;
		guest: boolean;
		loadout: Loadout | null;
		onbuy: () => void;
		ontry: () => void;
	} = $props();

	const baked = createBakedAvatar();

	$effect(() => {
		baked.set(loadout);
	});

	$effect(() => {
		return () => baked.destroy();
	});

	const locale = $derived(storeI18n.locale);
	const name = $derived(item ? shopItemText(itemLabelKey(item.id), item.id, locale) : "");
	const rarityName = $derived(
		item ? shopItemText(rarityLabelKey(item.rarity), item.rarity, locale) : ""
	);
	const featuredLabel = $derived(shopItemText("shop_featured", "Featured", locale));
	const buyLabel = $derived(
		guest
			? shopItemText("shop_register_to_buy", "Register to buy", locale)
			: shopItemText("shop_buy", "BUY", locale)
	);
	const tryLabel = $derived(shopItemText("shop_try_on", "TRY ON", locale));

	const buyDisabled = $derived(!item || guest || owned || balance < item.price);
</script>

{#if item}
	<div
		class="featured-banner"
		data-featured-id={item.id}
		style="--rarity-colour: {RARITY_COLOURS[item.rarity]}"
	>
		<span class="featured-tag font-tiny">{featuredLabel}</span>

		<div class="featured-preview pixel-bordered">
			{#if baked.url}
				<AvatarSprite url={baked.url} size={96} />
			{:else}
				<!-- The bake is not ready yet: an empty tile keeps the stage the same size. -->
				<span class="featured-item-art" aria-hidden="true"></span>
			{/if}
		</div>

		<div class="featured-info">
			<h2 class="featured-name font-heading">{name}</h2>
			<p class="featured-rarity font-tiny">{rarityName}</p>
			<p class="featured-price font-pixel">
				<span class="coin" aria-hidden="true"></span>{item.price}
			</p>
		</div>

		<div class="featured-actions">
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
		</div>
	</div>
{/if}

<style>
	.featured-banner {
		position: relative;
		display: flex;
		align-items: center;
		gap: 1rem;
		padding: 1rem;
		/* A deliberate break from the grid: the hero leans a little. */
		transform: skewX(-2deg);
		border: 2px solid var(--rarity-colour);
		background: linear-gradient(135deg, var(--surface-violet), var(--surface-deep));
	}

	.featured-tag {
		position: absolute;
		top: 0;
		left: 0;
		padding: 0.15rem 0.6rem;
		background: var(--rarity-colour);
		color: #111;
		letter-spacing: 0.08em;
		text-transform: uppercase;
	}

	.featured-preview {
		display: grid;
		place-items: center;
		flex: 0 0 auto;
		width: 120px;
		height: 120px;
		--pc-fill: var(--surface-deep);
	}

	.featured-item-art {
		display: block;
		width: 96px;
		height: 96px;
	}

	.featured-info {
		display: flex;
		flex: 1;
		flex-direction: column;
		gap: 0.25rem;
		min-width: 0;
	}

	.featured-name {
		margin: 0;
		min-width: 0;
		overflow-wrap: anywhere;
		color: var(--text-h);
	}

	.featured-rarity {
		margin: 0;
		text-transform: uppercase;
		letter-spacing: 0.08em;
		color: var(--rarity-colour);
	}

	.featured-price {
		display: flex;
		align-items: center;
		gap: 0.3rem;
		margin: 0;
		color: var(--gold);
	}

	.featured-actions {
		display: flex;
		flex-direction: column;
		gap: 0.5rem;
	}

	.featured-actions button:disabled {
		opacity: 0.5;
		cursor: not-allowed;
	}

	.coin {
		display: inline-block;
		width: 0.6em;
		height: 0.6em;
		border-radius: 50%;
		background: var(--gold);
	}
</style>
