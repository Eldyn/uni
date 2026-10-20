<!-- The Shop screen: a marketplace shell with a secondary sidebar, a hero row, a
     section tab row, three category tiles and the shelf, with the detail panels
     over the top. It reads the catalogue and the mock economy. The balance lives
     in the TopBar's chip; the screen keeps a local copy only to dress the detail
     panels. -->
<script lang="ts">
	import { getCatalogue } from "$lib/avatar/catalogue";
	import { defaultLoadout } from "$lib/avatar/defaultLoadout";
	import { defaultItemChoice } from "$lib/avatar/previewLoadout";
	import { onMount, tick } from "svelte";
	import { isBuyableNow, isSwatchBuyable } from "$lib/shop/shopBackend";
	import { compareRarity, paletteRarity } from "$lib/shop/rarity";
	import type { BuyFailure } from "$lib/shop/shopBackend";
	import type { AvatarSlot, CatalogueItem, Loadout, Palette, Swatch } from "$lib/avatar/types";
	import { storeShop } from "$lib/stores/shop.svelte";
	import { storeAuth } from "$stores/auth.svelte";
	import { storeAvatar } from "$stores/avatar.svelte";
	import { storeI18n } from "$stores/i18n.svelte";
	import { storeNavigation } from "$stores/navigation.svelte";
	import { shopItemText } from "$components/shop/ItemCard.svelte";
	import ItemCard from "$components/shop/ItemCard.svelte";
	import ItemDetail from "$components/shop/ItemDetail.svelte";
	import ShopBackdrop from "$components/shop/ShopBackdrop.svelte";
	import { HOME_BACKGROUND_HINT_CSS } from "$lib/shaders/homePalette";
	import ShopSidebar from "$components/shop/ShopSidebar.svelte";
	import ShopHero from "$components/shop/ShopHero.svelte";
	import CategoryTile from "$components/shop/CategoryTile.svelte";
	import SlotTabs from "$components/shop/SlotTabs.svelte";
	import PaletteCard from "$components/shop/PaletteCard.svelte";
	import PaletteDetail from "$components/shop/PaletteDetail.svelte";

	/** The marketplace sections: curated shelves, then the catalogue's groups. */
	type ShopSection =
		"featured" | "popular" | "new" | "wishlist" | "character" | "table" | "collections";

	type ShopCategory = "character" | "table" | "collections";

	interface SectionDef {
		id: ShopSection;
		key: string;
		fallback: string;
		group: "discover" | "categories";
	}

	const SECTION_DEFS: SectionDef[] = [
		{ id: "featured", key: "shop_nav_featured", fallback: "Featured", group: "discover" },
		{ id: "popular", key: "shop_nav_popular", fallback: "Popular", group: "discover" },
		{ id: "new", key: "shop_nav_new", fallback: "New", group: "discover" },
		{ id: "wishlist", key: "shop_nav_wishlist", fallback: "Wishlist", group: "discover" },
		{ id: "character", key: "shop_nav_character", fallback: "Character", group: "categories" },
		{ id: "table", key: "shop_nav_table", fallback: "Table", group: "categories" },
		{ id: "collections", key: "shop_nav_collections", fallback: "Collections", group: "categories" }
	];

	/** The hero row: one large tile plus a 2x2 grid of small ones. */
	const HERO_COUNT = 5;

	/** Shelf cards per row; the vertical arrow keys step a whole row. */
	const COLUMNS = 3;

	const CHARACTER_SLOTS: ReadonlySet<AvatarSlot> = new Set([
		"shape",
		"eyes",
		"mouth",
		"head_top",
		"mask",
		"bodywear",
		"motif"
	]);
	const TABLE_SLOTS: ReadonlySet<AvatarSlot> = new Set(["card_face", "card_back", "playmat"]);

	const CATEGORY_TILES: Array<{ id: ShopCategory; art: string; sprite: boolean }> = [
		{ id: "character", art: "/assets/base_player_strip.png", sprite: true },
		{ id: "table", art: "/assets/playmat.png", sprite: false },
		{ id: "collections", art: "/assets/avatar/set/uni_set_astronaut.png", sprite: true }
	];

	const catalogue = getCatalogue();
	// INFO: one "now" for the whole mount, so an item does not vanish mid-browse.
	const nowMs = Date.now();
	const locale = $derived(storeI18n.locale);
	const guest = $derived(storeAuth.isGuest);

	let balance = $state(storeShop.balance);
	let owned = $state<ReadonlySet<string>>(storeShop.owned);
	/** True once the account's session was rejected, so the screen hands off to login. */
	let needsLogin = $state(false);

	let section = $state<ShopSection>("featured");
	let selected = $state<CatalogueItem | null>(null);
	let selectedPalette = $state<Palette | null>(null);
	let buyError = $state<BuyFailure | null>(null);
	let focusedCard = $state(0);
	let gridEl: HTMLDivElement | undefined = $state();
	/** The card that opened the drawer, so closing it can return focus. */
	let triggerId = $state<string | null>(null);

	/** The preview look: TRY ON edits this copy, never the saved loadout. */
	let previewLoadout = $state<Loadout | null>(storeAvatar.loadout ?? defaultLoadout());

	/** The viewer's saved look: the shape and skin that dress each item tile. */
	const viewerLoadout = $derived(storeAvatar.loadout ?? defaultLoadout());

	const sectionLinks = $derived(
		SECTION_DEFS.map((def) => ({
			id: def.id,
			label: shopItemText(def.key, def.fallback, locale),
			group: def.group
		}))
	);
	const discoverLabel = $derived(shopItemText("shop_nav_discover", "Discover", locale));
	const tabsLabel = $derived(shopItemText("shop_tabs_label", "Shop categories", locale));
	const gridLabel = $derived(shopItemText("shop_grid_label", "Shop items", locale));
	const emptyLabel = $derived(shopItemText("shop_empty_slot", "Nothing here yet.", locale));

	const tabs = $derived(sectionLinks.map((link) => ({ id: link.id, label: link.label })));

	function categoryLabel(id: ShopCategory): string {
		return sectionLinks.find((link) => link.id === id)?.label ?? id;
	}

	/** Every item for sale now, plus every owned item. */
	const baseItems = $derived.by(() =>
		catalogue.items
			.filter((item) => isBuyableNow(item, nowMs) || owned.has(item.id))
			.sort((a, b) => compareRarity(a.rarity, b.rarity))
	);

	/** The section's buyable-now items, plus every owned item, as OWNED. */
	const items = $derived.by(() => {
		switch (section) {
			case "featured": {
				const featuredIds = new Set(catalogue.featured);
				if (featuredIds.size === 0) return baseItems;
				const chosen = baseItems.filter((item) => featuredIds.has(item.id));
				return chosen.length > 0 ? chosen : baseItems;
			}
			case "popular": {
				const order = new Map(storeShop.popular.map((entry, index) => [entry.item, index]));
				if (order.size === 0) return baseItems;
				return [...baseItems].sort(
					(a, b) => (order.get(a.id) ?? Infinity) - (order.get(b.id) ?? Infinity)
				);
			}
			case "new":
				return baseItems.filter((item) => item.starter !== true);
			case "wishlist": {
				const wanted = storeShop.wishlist;
				return baseItems.filter((item) => wanted.has(item.id));
			}
			case "character":
				return baseItems.filter((item) => CHARACTER_SLOTS.has(item.slot));
			case "table":
				return baseItems.filter((item) => TABLE_SLOTS.has(item.slot));
			case "collections":
				return baseItems.filter((item) => item.slot === "set");
		}
	});

	/** Paid palettes with a swatch for sale or owned, lowest rarity first. */
	const paletteCards = $derived(
		catalogue.palettes
			.filter((palette) => palette.starter !== true)
			.filter((palette) =>
				palette.swatches.some(
					(swatch) => isSwatchBuyable({ swatch, palette }) || owned.has(swatch.id)
				)
			)
			.sort((a, b) => compareRarity(paletteRarity(a), paletteRarity(b)))
	);

	/** The lowest price still for sale, or null when nothing is left to buy. */
	function lowestPrice(palette: Palette): number | null {
		const prices = palette.swatches
			.filter((swatch) => isSwatchBuyable({ swatch, palette }) && !owned.has(swatch.id))
			.map((swatch) => swatch.price ?? 0);
		return prices.length > 0 ? Math.min(...prices) : null;
	}

	/** Palettes are character colours, so they shelf beside the avatar items. */
	const paletteShelf = $derived(section === "character" ? paletteCards : []);
	const cardCount = $derived(items.length + paletteShelf.length);

	/** The hero row: featured first, then sets and collections, then the catalogue. */
	const heroItems = $derived.by(() => {
		const picked: CatalogueItem[] = [];
		const push = (item: CatalogueItem | undefined): void => {
			if (!item || picked.length >= HERO_COUNT) return;
			if (picked.some((entry) => entry.id === item.id)) return;
			picked.push(item);
		};

		for (const id of catalogue.featured) {
			push(baseItems.find((item) => item.id === id));
		}
		for (const item of baseItems) {
			if (item.slot === "set" || TABLE_SLOTS.has(item.slot)) push(item);
		}
		for (const item of baseItems) {
			push(item);
		}
		return picked;
	});

	const leadHero = $derived(heroItems[0] ?? null);
	const smallHeroes = $derived(heroItems.slice(1, HERO_COUNT));

	function selectSection(id: string): void {
		section = id as ShopSection;
		focusedCard = 0;
	}

	function openDetail(item: CatalogueItem): void {
		triggerId = item.id;
		selectedPalette = null;
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

	function openPalette(palette: Palette): void {
		triggerId = palette.id;
		selected = null;
		selectedPalette = palette;
		buyError = null;
	}

	async function closePalette(): Promise<void> {
		selectedPalette = null;
		buyError = null;
		const id = triggerId;
		await tick();
		const card = [...(gridEl?.querySelectorAll<HTMLElement>("[data-item-id]") ?? [])].find(
			(entry) => entry.dataset.itemId === id
		);
		card?.focus();
	}

	async function buySwatch(swatch: Swatch): Promise<void> {
		const result = await storeShop.buy(swatch.id);
		if (result.ok) {
			balance = result.balance;
			owned = new Set(storeShop.owned);
			buyError = null;
		} else {
			buyError = result.reason;
		}
	}

	/** The preview look with one item equipped, on a fresh copy. */
	function withItem(loadout: Loadout, item: CatalogueItem): Loadout {
		if (item.slot === "shape") return { ...loadout, shape: item.id };
		return {
			...loadout,
			slots: {
				...loadout.slots,
				[item.slot]: defaultItemChoice(item, loadout.skin, owned.has(item.id))
			}
		};
	}

	/** The hero art bakes the draft avatar already wearing the item. */
	function heroLoadout(item: CatalogueItem): Loadout {
		return withItem(previewLoadout ?? defaultLoadout(), item);
	}

	function tryOn(item: CatalogueItem): void {
		previewLoadout = withItem(previewLoadout ?? defaultLoadout(), item);
	}

	/** TRY ON for a color: the preview look wears it as the skin. */
	function tryColour(swatchId: string): void {
		previewLoadout = { ...(previewLoadout ?? defaultLoadout()), skin: swatchId };
		buyError = null;
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
		void storeShop.fetchWishlist();
		void storeShop.fetchPopular(7);
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
				focusCard(cardCount - 1);
				break;
		}
	}
</script>

<div class="shop-screen" style:background-color={HOME_BACKGROUND_HINT_CSS}>
	<ShopBackdrop dim={0.15} />

	<div class="shop-layout">
		<div class="shop-sidebar-slot">
			<ShopSidebar
				active={section}
				onselect={selectSection}
				{discoverLabel}
				categoriesLabel={tabsLabel}
				links={sectionLinks}
			/>
		</div>

		<main class="shop-main">
			{#if heroItems.length > 0}
				<div class="shop-hero-row">
					{#if leadHero}
						<ShopHero
							item={leadHero}
							size="large"
							owned={owned.has(leadHero.id)}
							{balance}
							{guest}
							loadout={heroLoadout(leadHero)}
							onbuy={() => buy(leadHero)}
							ontry={() => tryOn(leadHero)}
							onselect={() => openDetail(leadHero)}
						/>
					{/if}
					<div class="shop-hero-grid">
						{#each smallHeroes as hero (hero.id)}
							<ShopHero
								item={hero}
								size="small"
								owned={owned.has(hero.id)}
								{balance}
								{guest}
								loadout={heroLoadout(hero)}
								onbuy={() => buy(hero)}
								ontry={() => tryOn(hero)}
								onselect={() => openDetail(hero)}
							/>
						{/each}
					</div>
				</div>
			{/if}

			<SlotTabs {tabs} active={section} onselect={selectSection} label={tabsLabel} />

			<div class="shop-categories">
				{#each CATEGORY_TILES as tile (tile.id)}
					<CategoryTile
						category={tile.id}
						label={categoryLabel(tile.id)}
						art={tile.art}
						sprite={tile.sprite}
						onselect={() => selectSection(tile.id)}
					/>
				{/each}
			</div>

			{#if cardCount > 0}
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
							loadout={viewerLoadout}
							onselect={() => openDetail(item)}
						/>
					{/each}
					{#each paletteShelf as palette, index (palette.id)}
						<PaletteCard
							{palette}
							price={lowestPrice(palette)}
							selected={items.length + index === focusedCard}
							onselect={() => openPalette(palette)}
						/>
					{/each}
				</div>
			{:else}
				<p class="shop-empty font-tiny">{emptyLabel}</p>
			{/if}
		</main>
	</div>

	{#if selected}
		<ItemDetail
			item={selected}
			owned={owned.has(selected.id)}
			{balance}
			{guest}
			loadout={viewerLoadout}
			onbuy={() => buy(selected)}
			ontry={() => tryOn(selected)}
			onclose={closeDetail}
			error={buyError}
		/>
	{/if}

	{#if selectedPalette}
		<PaletteDetail
			palette={selectedPalette}
			{owned}
			{balance}
			{guest}
			loadout={viewerLoadout}
			onbuy={(swatch) => void buySwatch(swatch)}
			ontry={tryColour}
			onclose={closePalette}
			error={buyError}
		/>
	{/if}
</div>

<style>
	.shop-screen {
		position: relative;
		display: flex;
		flex-direction: column;
		gap: 1rem;
		/* Fill the shell's main pane and scroll inside it, so the backdrop and
		   the shelves never spill past the TopBar/NavBar chrome. */
		height: 100%;
		min-height: 0;
		overflow-y: auto;
		padding: 1rem;
	}

	.shop-layout {
		position: relative;
		z-index: 1;
		display: grid;
		grid-template-columns: 14rem minmax(0, 1fr);
		gap: 1rem;
		align-items: start;
	}

	.shop-sidebar-slot {
		min-width: 0;
	}

	.shop-main {
		display: flex;
		flex-direction: column;
		gap: 1rem;
		min-width: 0;
	}

	.shop-hero-row {
		display: grid;
		grid-template-columns: minmax(0, 2fr) minmax(0, 1fr);
		gap: 0.75rem;
		align-items: stretch;
	}

	.shop-hero-grid {
		display: grid;
		grid-template-columns: repeat(2, minmax(0, 1fr));
		grid-template-rows: repeat(2, minmax(0, 1fr));
		gap: 0.5rem;
	}

	.shop-categories {
		display: grid;
		grid-template-columns: repeat(3, minmax(0, 1fr));
		gap: 0.75rem;
	}

	.shop-grid {
		display: grid;
		grid-template-columns: repeat(3, minmax(0, 1fr));
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

	@media (max-width: 900px) {
		.shop-layout {
			grid-template-columns: minmax(0, 1fr);
		}

		.shop-sidebar-slot {
			display: none;
		}

		.shop-hero-row {
			grid-template-columns: minmax(0, 1fr);
		}

		.shop-categories,
		.shop-grid {
			grid-template-columns: repeat(2, minmax(0, 1fr));
		}
	}

	@media (max-width: 600px) {
		.shop-categories,
		.shop-grid {
			grid-template-columns: minmax(0, 1fr);
		}
	}
</style>
