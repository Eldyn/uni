<!-- The Shop screen: a shader backdrop, the featured hero, the slot tabs and
     the item grid, with the detail panel over the top. It reads the catalogue
     and the mock economy. The balance chip lives here (not the TopBar, which is
     not ours to edit) and shows the amount spent as soon as a buy succeeds. -->
<script lang="ts">
	import { getCatalogue } from "$lib/avatar/catalogue";
	import { defaultLoadout } from "$lib/avatar/defaultLoadout";
	import { onMount, tick } from "svelte";
	import { isBuyableNow } from "$lib/shop/shopBackend";
	import type { BuyFailure } from "$lib/shop/shopBackend";
	import type { AvatarSlot, CatalogueItem, Loadout } from "$lib/avatar/types";
	import { storeShop } from "$lib/stores/shop.svelte";
	import { storeAuth } from "$stores/auth.svelte";
	import { storeAvatar } from "$stores/avatar.svelte";
	import { storeI18n } from "$stores/i18n.svelte";
	import { storeNavigation } from "$stores/navigation.svelte";
	import { shopItemText } from "$components/shop/ItemCard.svelte";
	import ItemCard from "$components/shop/ItemCard.svelte";
	import ItemDetail from "$components/shop/ItemDetail.svelte";
	import ShopBackdrop from "$components/shop/ShopBackdrop.svelte";
	import FeaturedBanner from "$components/shop/FeaturedBanner.svelte";
	import SlotTabs from "$components/shop/SlotTabs.svelte";

	/** A tab either filters to one catalogue slot, or to palettes, or to all. */
	type TabSlot = AvatarSlot | "palette" | null;

	interface TabDef {
		id: string;
		key: string;
		fallback: string;
		slot: TabSlot;
	}

	const TAB_DEFS: TabDef[] = [
		{ id: "all", key: "shop_tab_all", fallback: "All", slot: null },
		{ id: "eyes", key: "shop_tab_eyes", fallback: "Eyes", slot: "eyes" },
		{ id: "mouth", key: "shop_tab_mouths", fallback: "Mouths", slot: "mouth" },
		{ id: "head_top", key: "shop_tab_hats", fallback: "Hats", slot: "head_top" },
		{ id: "mask", key: "shop_tab_masks", fallback: "Masks", slot: "mask" },
		{ id: "bodywear", key: "shop_tab_body", fallback: "Body", slot: "bodywear" },
		{ id: "set", key: "shop_tab_sets", fallback: "Sets", slot: "set" },
		{ id: "motif", key: "shop_tab_motifs", fallback: "Motifs", slot: "motif" },
		{ id: "palettes", key: "shop_tab_palettes", fallback: "Palettes", slot: "palette" }
	];

	/** Cards per row; the vertical arrow keys step a whole row. */
	const COLUMNS = 6;

	const catalogue = getCatalogue();
	// INFO: one "now" for the whole mount, so an item does not vanish mid-browse.
	const nowMs = Date.now();
	const locale = $derived(storeI18n.locale);
	const guest = $derived(storeAuth.isGuest);

	let balance = $state(storeShop.balance);
	let owned = $state<ReadonlySet<string>>(storeShop.owned);
	/** True once the account's session was rejected, so the screen hands off to login. */
	let needsLogin = $state(false);

	let activeTab = $state("all");
	let selected = $state<CatalogueItem | null>(null);
	let buyError = $state<BuyFailure | null>(null);
	let focusedCard = $state(0);
	let gridEl: HTMLDivElement | undefined = $state();
	/** The card that opened the drawer, so closing it can return focus. */
	let triggerId = $state<string | null>(null);

	/** The preview look: TRY ON edits this copy, never the saved loadout. */
	let previewLoadout = $state<Loadout | null>(storeAvatar.loadout ?? defaultLoadout());

	const tabs = $derived(
		TAB_DEFS.map((tab) => ({ id: tab.id, label: shopItemText(tab.key, tab.fallback, locale) }))
	);

	const title = $derived(shopItemText("shop_title", "Shop", locale));
	const tabsLabel = $derived(shopItemText("shop_tabs_label", "Shop categories", locale));
	const gridLabel = $derived(shopItemText("shop_grid_label", "Shop items", locale));
	const emptyLabel = $derived(shopItemText("shop_empty_slot", "Nothing here yet.", locale));

	/** The chosen tab's buyable-now items, plus every owned item, as OWNED. */
	const items = $derived.by(() => {
		const tab = TAB_DEFS.find((entry) => entry.id === activeTab);
		return catalogue.items.filter((item) => {
			if (tab && tab.slot !== null && item.slot !== tab.slot) return false;
			return isBuyableNow(item, nowMs) || owned.has(item.id);
		});
	});

	const featured = $derived.by(() => {
		const id = catalogue.featured[0];
		return id ? (catalogue.items.find((item) => item.id === id) ?? null) : null;
	});
	const featuredOwned = $derived(featured ? owned.has(featured.id) : false);

	function selectTab(id: string): void {
		activeTab = id;
		focusedCard = 0;
	}

	function openDetail(item: CatalogueItem): void {
		triggerId = item.id;
		selected = item;
		buyError = null;
	}

	async function closeDetail(): Promise<void> {
		selected = null;
		buyError = null;
		const id = triggerId;
		// INFO: let the drawer unmount before moving focus back to the grid.
		await tick();
		const card = [...(gridEl?.querySelectorAll<HTMLElement>("[data-item-id]") ?? [])].find(
			(entry) => entry.dataset.itemId === id
		);
		card?.focus();
	}

	/** The preview look with one item equipped, on a fresh copy. */
	function withItem(loadout: Loadout, item: CatalogueItem): Loadout {
		if (item.slot === "shape") return { ...loadout, shape: item.id };
		return {
			...loadout,
			slots: { ...loadout.slots, [item.slot]: { item: item.id, colours: [] } }
		};
	}

	/** The featured hero shows the draft avatar already wearing the item. */
	const featuredPreview = $derived(
		featured ? withItem(previewLoadout ?? defaultLoadout(), featured) : previewLoadout
	);

	function tryOn(item: CatalogueItem): void {
		previewLoadout = withItem(previewLoadout ?? defaultLoadout(), item);
	}

	async function buy(item: CatalogueItem): Promise<void> {
		const result = await storeShop.buy(item.id);
		if (result.ok) {
			balance = result.balance;
			// INFO: a fresh Set, so the grid's OWNED ribbons re-render.
			owned = new Set(storeShop.owned);
			buyError = null;
		} else {
			buyError = result.reason;
		}
	}

	onMount(async () => {
		if (guest) return;
		await storeShop.fetchMine?.();
		balance = storeShop.balance;
		owned = new Set(storeShop.owned);
		needsLogin = storeShop.needsLogin === true;
	});

	$effect(() => {
		// INFO: an expired session goes to the login flow, not a blank shop.
		if (needsLogin) storeNavigation.gotoAuth("login");
	});

	function focusCard(index: number): void {
		const cards = gridEl?.querySelectorAll<HTMLElement>("[data-item-id]");
		if (!cards || cards.length === 0) return;
		const next = (index + cards.length) % cards.length;
		focusedCard = next;
		cards[next]?.focus();
	}

	function handleGridFocus(event: FocusEvent): void {
		const cards = gridEl?.querySelectorAll<HTMLElement>("[data-item-id]");
		if (!cards) return;
		const index = [...cards].indexOf(event.target as HTMLElement);
		if (index >= 0) focusedCard = index;
	}

	function handleGridKeydown(event: KeyboardEvent): void {
		switch (event.key) {
			case "ArrowRight":
				event.preventDefault();
				focusCard(focusedCard + 1);
				break;
			case "ArrowLeft":
				event.preventDefault();
				focusCard(focusedCard - 1);
				break;
			case "ArrowDown":
				event.preventDefault();
				focusCard(focusedCard + COLUMNS);
				break;
			case "ArrowUp":
				event.preventDefault();
				focusCard(focusedCard - COLUMNS);
				break;
			case "Home":
				event.preventDefault();
				focusCard(0);
				break;
			case "End":
				event.preventDefault();
				focusCard(items.length - 1);
				break;
		}
	}
</script>

<div class="shop-screen">
	<ShopBackdrop dim={0.15} />

	<header class="shop-header">
		<h1 class="shop-title title-screen font-heading">{title}</h1>
		<span class="shop-balance font-pixel" data-balance={balance}>
			<span class="coin" aria-hidden="true"></span>{balance}
		</span>
	</header>

	{#if featured}
		<FeaturedBanner
			item={featured}
			owned={featuredOwned}
			{balance}
			{guest}
			loadout={featuredPreview}
			onbuy={() => buy(featured)}
			ontry={() => tryOn(featured)}
		/>
	{/if}

	{#if selected}
		<ItemDetail
			item={selected}
			owned={owned.has(selected.id)}
			{balance}
			{guest}
			onbuy={() => buy(selected)}
			ontry={() => tryOn(selected)}
			onclose={closeDetail}
			error={buyError}
		/>
	{/if}

	<SlotTabs {tabs} active={activeTab} onselect={selectTab} label={tabsLabel} />

	{#if items.length > 0}
		<div
			class="shop-grid"
			role="grid"
			tabindex="-1"
			aria-label={gridLabel}
			bind:this={gridEl}
			onfocusin={handleGridFocus}
			onkeydown={handleGridKeydown}
		>
			{#each items as item, index (item.id)}
				<ItemCard
					{item}
					owned={owned.has(item.id)}
					selected={index === focusedCard}
					onselect={() => openDetail(item)}
				/>
			{/each}
		</div>
	{:else}
		<p class="shop-empty font-tiny">{emptyLabel}</p>
	{/if}
</div>

<style>
	.shop-screen {
		position: relative;
		display: flex;
		flex-direction: column;
		gap: 1rem;
		/* Fill the shell's main pane and scroll inside it, so the backdrop and
		   the grid never spill past the TopBar/NavBar chrome. */
		height: 100%;
		min-height: 0;
		overflow-y: auto;
		padding: 1rem;
	}

	.shop-header,
	.shop-grid,
	.shop-empty {
		position: relative;
		z-index: 1;
	}

	.shop-header {
		display: flex;
		align-items: center;
		justify-content: space-between;
		gap: 1rem;
	}

	.shop-title {
		margin: 0;
	}

	.shop-balance {
		display: flex;
		align-items: center;
		gap: 0.4rem;
		padding: 0.35rem 0.7rem;
		border: 2px solid var(--gold);
		background: var(--surface-deep);
		color: var(--gold);
	}

	.shop-grid {
		display: grid;
		grid-template-columns: repeat(6, minmax(0, 1fr));
		gap: 0.75rem;
	}

	.shop-empty {
		position: relative;
		z-index: 1;
		margin: 0;
		padding: 2rem 1rem;
		text-align: center;
		color: var(--text);
	}

	.coin {
		display: inline-block;
		width: 0.7em;
		height: 0.7em;
		border-radius: 50%;
		background: var(--gold);
	}

	@media (max-width: 900px) {
		.shop-grid {
			grid-template-columns: repeat(3, minmax(0, 1fr));
		}
	}
</style>
