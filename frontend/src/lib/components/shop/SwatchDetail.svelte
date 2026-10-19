<!-- The swatch detail panel: the color tile on its rarity frame, the name,
     the palette, the price, and BUY. Escape closes the panel. -->
<script lang="ts">
	import { onMount } from "svelte";
	import RarityFrame from "$components/shop/RarityFrame.svelte";
	import { shopItemText } from "$components/shop/ItemCard.svelte";
	import { BUY_FAILURE_TEXT } from "$lib/shop/buyFailure";
	import { swatchName } from "$lib/shop/swatchText";
	import { RARITY_COLOURS, rarityLabelColour, rarityLabelKey } from "$lib/shop/rarity";
	import type { BuyFailure } from "$lib/shop/shopBackend";
	import type { Palette, Swatch } from "$lib/avatar/types";
	import { storeI18n } from "$stores/i18n.svelte";

	let {
		swatch,
		palette,
		owned,
		balance,
		guest,
		onbuy,
		onclose,
		error = null
	}: {
		swatch: Swatch;
		palette: Palette;
		owned: boolean;
		balance: number;
		guest: boolean;
		onbuy: () => void;
		onclose: () => void;
		error?: BuyFailure | null;
	} = $props();

	const locale = $derived(storeI18n.locale);
	const name = $derived(swatchName(swatch.id));
	const paletteKey = $derived(`shop_palette_${palette.id.replaceAll(":", "_")}`);
	const paletteName = $derived(shopItemText(paletteKey, palette.name, locale));
	const rarity = $derived(swatch.rarity ?? "common");
	const rarityName = $derived(shopItemText(rarityLabelKey(rarity), rarity, locale));
	const price = $derived(swatch.price ?? 0);

	const buyLabel = $derived(
		guest
			? shopItemText("shop_register_to_buy", "Register to buy", locale)
			: shopItemText("shop_buy", "BUY", locale)
	);
	const closeLabel = $derived(shopItemText("shop_close", "Close", locale));
	const tooPoor = $derived(!guest && !owned && balance < price);
	const buyDisabled = $derived(guest || owned || tooPoor);
	const reason = $derived(
		owned
			? shopItemText("shop_error_already_owned", "You already own this.", locale)
			: tooPoor
				? shopItemText("shop_insufficient_cash", "Not enough cash", locale)
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
	class="swatch-detail"
	role="dialog"
	aria-label={name}
	tabindex="-1"
	style="--rarity-colour: {RARITY_COLOURS[rarity]}"
>
	<button type="button" class="detail-close font-tiny" aria-label={closeLabel} onclick={onclose}>
		<span aria-hidden="true">×</span>
	</button>

	<div class="detail-art">
		<RarityFrame {rarity}>
			<span class="detail-swatch" style="--swatch: {swatch.hex}" aria-hidden="true"></span>
		</RarityFrame>
	</div>

	<h2 class="detail-name font-heading">{name}</h2>
	<p class="detail-palette font-tiny">{paletteName}</p>
	<p class="detail-rarity font-tiny" style="color: {rarityLabelColour(rarity)}">{rarityName}</p>
	<p class="detail-price font-pixel">
		<span class="coin" aria-hidden="true"></span>{price}
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
	</div>

	{#if reason}
		<p class="detail-reason font-tiny">{reason}</p>
	{/if}
</div>

<style>
	.swatch-detail {
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
		width: 112px;
		height: 112px;
	}

	.detail-art :global(.rarity-frame) {
		flex: 1;
		align-items: center;
		justify-content: center;
	}

	.detail-swatch {
		display: block;
		width: 96px;
		height: 96px;
		background: var(--swatch);
	}

	.detail-name {
		margin: 0;
		min-width: 0;
		overflow-wrap: anywhere;
		text-align: center;
		font-size: 1rem;
		color: var(--text-h);
	}

	.detail-palette,
	.detail-rarity {
		margin: 0;
		text-align: center;
	}

	.detail-rarity {
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
