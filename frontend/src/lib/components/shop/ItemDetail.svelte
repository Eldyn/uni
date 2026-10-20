<!-- The item detail panel: the art on its rarity frame, the name, rarity and
     price, and the BUY / TRY ON actions. BUY is disabled with a reason line
     when the item is owned or the balance falls short, and a guest sees
     "Register to buy" instead. Escape closes the panel. -->
<script lang="ts">
	import { onMount } from "svelte";
	import AvatarFit from "$components/common/AvatarFit.svelte";
	import BlankIcon from "$components/common/BlankIcon.svelte";
	import RarityFrame from "$components/shop/RarityFrame.svelte";
	import { itemLabelKey, shopItemText } from "$components/shop/ItemCard.svelte";
	import { itemPreviewLoadout } from "$lib/avatar/previewLoadout";
	import { storeI18n } from "$stores/i18n.svelte";
	import { RARITY_COLOURS, rarityLabelColour, rarityLabelKey } from "$lib/shop/rarity";
	import { BUY_FAILURE_TEXT } from "$lib/shop/buyFailure";
	import type { BuyFailure } from "$lib/shop/shopBackend";
	import type { CatalogueItem, Loadout } from "$lib/avatar/types";

	let {
		item,
		owned,
		balance,
		guest,
		loadout,
		onbuy,
		ontry,
		onclose,
		error = null
	}: {
		item: CatalogueItem;
		owned: boolean;
		balance: number;
		guest: boolean;
		/** The viewer's saved look: its shape and skin dress the preview. */
		loadout: Loadout;
		onbuy: () => void;
		ontry: () => void;
		onclose: () => void;
		error?: BuyFailure | null;
	} = $props();

	const locale = $derived(storeI18n.locale);
	const currencyName = $derived(shopItemText("shop_currency_name", "Blank", locale));
	const name = $derived(shopItemText(itemLabelKey(item.id), item.id, locale));
	const rarityName = $derived(shopItemText(rarityLabelKey(item.rarity), item.rarity, locale));
	const preview = $derived(itemPreviewLoadout(item, loadout, owned));

	const buyLabel = $derived(
		guest
			? shopItemText("shop_register_to_buy", "Register to buy", locale)
			: shopItemText("shop_buy", "BUY", locale)
	);
	const tryLabel = $derived(shopItemText("shop_try_on", "TRY ON", locale));
	const closeLabel = $derived(shopItemText("shop_close", "Close", locale));

	const tooPoor = $derived(!guest && !owned && balance < item.price);
	const buyDisabled = $derived(guest || owned || tooPoor);
	const reason = $derived(
		owned
			? shopItemText("shop_error_already_owned", "You already own this.", locale)
			: tooPoor
				? shopItemText("shop_insufficient_cash", "Not enough Blank", locale)
				: null
	);
	const failure = $derived(
		error ? shopItemText(BUY_FAILURE_TEXT[error][0], BUY_FAILURE_TEXT[error][1], locale) : null
	);

	onMount(() => {
		function handleKeydown(event: KeyboardEvent): void {
			if (event.key === "Escape") onclose();
		}
		window.addEventListener("keydown", handleKeydown);
		return () => window.removeEventListener("keydown", handleKeydown);
	});
</script>

<div
	class="item-detail"
	role="dialog"
	aria-label={name}
	tabindex="-1"
	style="--rarity-colour: {RARITY_COLOURS[item.rarity]}"
>
	<button type="button" class="detail-close font-tiny" aria-label={closeLabel} onclick={onclose}>
		<span aria-hidden="true">×</span>
	</button>

	<div class="detail-art">
		<RarityFrame rarity={item.rarity}>
			<span class="item-art" aria-hidden="true"><AvatarFit loadout={preview} /></span>
		</RarityFrame>
	</div>

	<h2 class="detail-name font-heading">{name}</h2>
	<p class="detail-rarity font-tiny" style="color: {rarityLabelColour(item.rarity)}">
		{rarityName}
	</p>
	<p class="detail-price font-pixel">
		<BlankIcon label={currencyName} />{item.price}
	</p>

	{#if failure}
		<p class="detail-error font-tiny" role="alert">{failure}</p>
	{/if}

	<div class="detail-actions">
		<button
			type="button"
			class="btn detail-buy"
			disabled={buyDisabled}
			onclick={() => {
				if (!buyDisabled) onbuy();
			}}
		>
			{buyLabel}
		</button>
		<button type="button" class="btn-secondary detail-try" onclick={ontry}>{tryLabel}</button>
	</div>

	{#if reason}
		<p class="detail-reason font-tiny">{reason}</p>
	{/if}
</div>

<style>
	.item-detail {
		position: fixed;
		top: 0;
		right: 0;
		bottom: 0;
		z-index: 40;
		display: flex;
		flex-direction: column;
		align-items: center;
		gap: 0.4rem;
		width: min(360px, 92vw);
		min-width: 0;
		padding: 1.5rem 1rem;
		overflow-y: auto;
		border-left: 2px solid var(--rarity-colour);
		background: var(--surface);
		/* A rarity-coloured glow marks the drawer's edge over the grid. */
		box-shadow: -8px 0 24px color-mix(in srgb, var(--rarity-colour) 45%, transparent);
	}

	.detail-close {
		position: absolute;
		top: 0.25rem;
		right: 0.25rem;
		padding: 0.1rem 0.4rem;
		border: none;
		background: none;
		color: var(--text);
		cursor: pointer;
	}

	.detail-art {
		display: flex;
		width: 208px;
		height: 208px;
	}

	.detail-art :global(.rarity-frame) {
		flex: 1;
		align-items: center;
		justify-content: center;
	}

	.item-art {
		display: block;
		width: 100%;
		height: 100%;
	}

	.detail-name {
		margin: 0;
		min-width: 0;
		overflow-wrap: anywhere;
		text-align: center;
		font-size: 1rem;
		color: var(--text-h);
	}

	.detail-rarity {
		margin: 0;
		text-transform: uppercase;
		letter-spacing: 0.08em;
	}

	.detail-price {
		display: flex;
		align-items: center;
		gap: 0.3rem;
		margin: 0;
		color: var(--accent);
	}

	.detail-error {
		margin: 0;
		color: var(--danger, #ff6b6b);
		text-align: center;
	}

	.detail-actions {
		display: flex;
		gap: 0.5rem;
		margin-top: 0.25rem;
	}

	.detail-actions button:disabled {
		opacity: 0.5;
		cursor: not-allowed;
	}

	.detail-reason {
		margin: 0;
		color: var(--text);
		text-align: center;
	}
</style>
