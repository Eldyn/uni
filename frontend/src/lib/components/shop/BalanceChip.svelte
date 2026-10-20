<!-- The account's Blank balance: the currency icon and the amount in a bordered
     chip. Lives in the top bar, so every screen shows the same wallet. -->
<script lang="ts">
	import BlankIcon from "$components/common/BlankIcon.svelte";
	import { shopItemText } from "$components/shop/ItemCard.svelte";
	import { storeI18n } from "$stores/i18n.svelte";

	let { balance }: { balance: number } = $props();

	const locale = $derived(storeI18n.locale);
	const currencyName = $derived(shopItemText("shop_currency_name", "Blank", locale));
</script>

<span class="shop-balance font-pixel" data-balance={balance}>
	<BlankIcon label={currencyName} />{balance}
</span>

<style>
	.shop-balance {
		display: flex;
		align-items: center;
		gap: 0.4rem;
		padding: 0.35rem 0.7rem;
		border: 2px solid var(--accent);
		background: var(--surface-deep);
		color: var(--accent);
	}
</style>
