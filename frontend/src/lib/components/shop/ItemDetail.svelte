<!-- The item detail panel: the art on its rarity frame, the name, rarity and
     price, and the BUY / TRY ON actions. BUY is disabled with a reason line
     when the item is owned or the balance falls short, and a guest sees
     "Register to buy" instead. Escape closes the panel. -->
<script lang="ts">
	import { onMount } from "svelte";
	import RarityFrame from "$components/shop/RarityFrame.svelte";
	import { itemArtStyle, itemLabelKey, shopItemText } from "$components/shop/ItemCard.svelte";
	import { storeI18n } from "$stores/i18n.svelte";
	import { RARITY_COLOURS, rarityLabelKey } from "$lib/shop/rarity";
	import type { BuyFailure } from "$lib/shop/shopBackend";
	import type { CatalogueItem } from "$lib/avatar/types";

	let {
		item,
		owned,
		balance,
		guest,
		onbuy,
		ontry,
		onclose,
		error = null
	}: {
		item: CatalogueItem;
		owned: boolean;
		balance: number;
		guest: boolean;
		onbuy: () => void;
		ontry: () => void;
		onclose: () => void;
		error?: BuyFailure | null;
	} = $props();

	/** The English line shown for each failure, with the key Task 8 will fill in. */
	const FAILURE_TEXT: Record<BuyFailure, [key: string, fallback: string]> = {
		insufficient_cash: ["shop_error_insufficient_cash", "Not enough cash."],
		already_owned: ["shop_error_already_owned", "You already own this."],
		not_available: ["shop_error_not_available", "This item is no longer available."],
		unknown_item: ["shop_error_unknown_item", "This item does not exist."],
		guest: ["shop_error_guest", "Register to buy."]
	};

	const locale = $derived(storeI18n.locale);
	const name = $derived(shopItemText(itemLabelKey(item.id), item.id, locale));
	const rarityName = $derived(shopItemText(rarityLabelKey(item.rarity), item.rarity, locale));
	const artStyle = $derived(itemArtStyle(item));

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
				? shopItemText("shop_insufficient_cash", "Not enough cash", locale)
				: null
	);
	const failure = $derived(
		error ? shopItemText(FAILURE_TEXT[error][0], FAILURE_TEXT[error][1], locale) : null
	);

	onMount(() => {
		function handleKeydown(event: KeyboardEvent): void {
			if (event.key === "Escape") onclose();
		}
		window.addEventListener("keydown", handleKeydown);
		return () => window.removeEventListener("keydown", handleKeydown);
	});
</script>

<div class="item-detail" role="dialog" aria-label={name} tabindex="-1">
	<button type="button" class="detail-close font-tiny" aria-label={closeLabel} onclick={onclose}>
		<span aria-hidden="true">×</span>
	</button>

	<div class="detail-art">
		<RarityFrame rarity={item.rarity}>
			<span class="item-art" style={artStyle} aria-hidden="true"></span>
		</RarityFrame>
	</div>

	<h2 class="detail-name font-heading">{name}</h2>
	<p class="detail-rarity font-tiny" style="color: {RARITY_COLOURS[item.rarity]}">{rarityName}</p>
	<p class="detail-price font-pixel">
		<span class="coin" aria-hidden="true"></span>{item.price}
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
		position: relative;
		display: flex;
		flex-direction: column;
		align-items: center;
		gap: 0.4rem;
		min-width: 0;
		padding: 1rem;
		border: 2px solid var(--border);
		background: var(--surface);
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
		width: 112px;
		height: 112px;
	}

	.detail-art :global(.rarity-frame) {
		flex: 1;
		align-items: center;
		justify-content: center;
	}

	.item-art {
		width: 96px;
		height: 96px;
		background-repeat: no-repeat;
		background-position: 0 0;
		image-rendering: pixelated;
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
		color: var(--gold);
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

	.coin {
		display: inline-block;
		width: 0.6em;
		height: 0.6em;
		border-radius: 50%;
		background: var(--gold);
	}
</style>
