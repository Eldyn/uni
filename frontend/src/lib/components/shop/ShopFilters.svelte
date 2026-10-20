<!-- The shelf toolbar: rarity, name search and sort. The screen owns the values
     and applies them to the grid; this component only reports changes. -->
<script module lang="ts">
	import type { Rarity } from "$lib/avatar/types";

	export type ShopSort = "popular" | "wishlist" | "price" | "name";

	export type ShopRarityFilter = Rarity | "all";
</script>

<script lang="ts">
	import { RARITIES } from "$lib/avatar/types";
	import { rarityLabelKey } from "$lib/shop/rarity";
	import { shopItemText } from "$components/shop/ItemCard.svelte";
	import { storeI18n } from "$stores/i18n.svelte";

	let {
		rarity = $bindable("all"),
		query = $bindable(""),
		sort = $bindable("popular")
	}: {
		rarity?: ShopRarityFilter;
		query?: string;
		sort?: ShopSort;
	} = $props();

	const locale = $derived(storeI18n.locale);
	const rarityLabel = $derived(shopItemText("shop_filter_rarity", "Rarity", locale));
	const nameLabel = $derived(shopItemText("shop_filter_name", "Name", locale));
	const sortLabel = $derived(shopItemText("shop_filter_sort", "Sort", locale));
	const allLabel = $derived(shopItemText("shop_tab_all", "All", locale));

	const rarityOptions = $derived(
		RARITIES.map((value) => ({
			value,
			label: shopItemText(rarityLabelKey(value), value, locale)
		}))
	);

	const sortOptions = $derived<Array<{ value: ShopSort; label: string }>>([
		{ value: "popular", label: shopItemText("shop_sort_popular", "Popular", locale) },
		{ value: "wishlist", label: shopItemText("shop_sort_wishlist", "Wishlist count", locale) },
		{ value: "price", label: shopItemText("shop_sort_price", "Price", locale) },
		{ value: "name", label: shopItemText("shop_sort_name", "Name", locale) }
	]);
</script>

<div class="shop-filters">
	<label class="shop-filter">
		<span class="shop-filter-label font-tiny">{rarityLabel}</span>
		<select class="shop-filter-select font-tiny" data-filter="rarity" bind:value={rarity}>
			<option value="all">{allLabel}</option>
			{#each rarityOptions as option (option.value)}
				<option value={option.value}>{option.label}</option>
			{/each}
		</select>
	</label>

	<label class="shop-filter shop-filter-search">
		<span class="shop-filter-label font-tiny">{nameLabel}</span>
		<input
			class="shop-filter-input font-tiny"
			data-filter="name"
			type="search"
			bind:value={query}
		/>
	</label>

	<label class="shop-filter">
		<span class="shop-filter-label font-tiny">{sortLabel}</span>
		<select class="shop-filter-select font-tiny" data-filter="sort" bind:value={sort}>
			{#each sortOptions as option (option.value)}
				<option value={option.value}>{option.label}</option>
			{/each}
		</select>
	</label>
</div>

<style>
	.shop-filters {
		position: relative;
		z-index: 1;
		display: flex;
		flex-wrap: wrap;
		align-items: flex-end;
		gap: 0.75rem;
	}

	.shop-filter {
		display: flex;
		flex-direction: column;
		gap: 0.2rem;
		min-width: 0;
	}

	.shop-filter-label {
		color: var(--text);
		letter-spacing: 0.06em;
		text-transform: uppercase;
	}

	.shop-filter-select,
	.shop-filter-input {
		min-width: 8rem;
		padding: 0.35rem 0.45rem;
		border: 1px solid var(--surface-deep);
		border-radius: 0;
		background: var(--surface);
		color: var(--text-h);
	}

	.shop-filter-search {
		flex: 1;
	}

	.shop-filter-input {
		width: 100%;
	}
</style>
