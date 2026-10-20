<!-- The marketplace's secondary sidebar: a Discover group of curated shelves
     and a categories group for the catalogue's own sections. The active section
     is the current page. -->
<script module lang="ts">
	export type ShopSection =
		"featured" | "popular" | "new" | "wishlist" | "character" | "table" | "collections";

	export type ShopNavGroup = "discover" | "categories";

	export interface ShopNavLink {
		id: ShopSection;
		label: string;
		group: ShopNavGroup;
	}
</script>

<script lang="ts">
	let {
		active,
		onselect,
		discoverLabel,
		categoriesLabel,
		links
	}: {
		active: ShopSection;
		onselect: (section: ShopSection) => void;
		discoverLabel: string;
		categoriesLabel: string;
		links: readonly ShopNavLink[];
	} = $props();

	const discover = $derived(links.filter((link) => link.group === "discover"));
	const categories = $derived(links.filter((link) => link.group === "categories"));
</script>

<nav class="shop-sidebar" aria-label={categoriesLabel}>
	<div class="shop-nav-group" role="group" aria-label={discoverLabel}>
		<h2 class="shop-nav-heading">{discoverLabel}</h2>
		{#each discover as link (link.id)}
			<button
				type="button"
				class="shop-nav-link"
				data-section={link.id}
				aria-current={active === link.id ? "page" : undefined}
				onclick={() => onselect(link.id)}
			>
				{link.label}
			</button>
		{/each}
	</div>

	<div class="shop-nav-group" role="group" aria-label={categoriesLabel}>
		{#each categories as link (link.id)}
			<button
				type="button"
				class="shop-nav-link"
				data-section={link.id}
				aria-current={active === link.id ? "page" : undefined}
				onclick={() => onselect(link.id)}
			>
				{link.label}
			</button>
		{/each}
	</div>
</nav>

<style>
	.shop-sidebar {
		position: sticky;
		top: 0;
		display: flex;
		flex-direction: column;
		gap: 1.25rem;
		padding: 0.5rem 0;
	}

	.shop-nav-group {
		display: flex;
		flex-direction: column;
		gap: 0.15rem;
	}

	.shop-nav-heading {
		font-family: var(--pypx);
		font-weight: 800;
		margin: 0 0 0.35rem;
		padding: 0 0.5rem;
		letter-spacing: 0.08em;
		text-transform: uppercase;
		color: var(--text-h);
	}

	.shop-nav-link {
		padding: 0.45rem 0.5rem;
		border: none;
		border-left: 2px solid transparent;
		background: none;
		font-family: var(--pypx);
		font-weight: 800;
		text-align: left;
		letter-spacing: 0.04em;
		color: var(--text);
		cursor: pointer;
		clip-path: none;
	}

	.shop-nav-link:hover {
		color: var(--text-h);
		border-left-color: var(--surface-deep);
	}

	.shop-nav-link[aria-current="page"] {
		color: var(--accent);
		border-left-color: var(--accent);
	}
</style>
