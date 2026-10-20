<!-- A large marketplace category tile: the section's art fills the tile and its
     title sits over it. Pressing the tile opens that category's shelf. -->
<script module lang="ts">
	export type ShopCategory = "character" | "table" | "collections";
</script>

<script lang="ts">
	import AvatarSprite from "$components/common/AvatarSprite.svelte";
	import { assetUrl } from "$lib/utils/assetUrl";

	let {
		category,
		label,
		art = "",
		sprite = false,
		onselect
	}: {
		category: ShopCategory;
		label: string;
		art?: string;
		sprite?: boolean;
		onselect: () => void;
	} = $props();
</script>

<button
	type="button"
	class="category-tile pixel-bordered"
	data-category={category}
	onclick={onselect}
>
	<span class="category-art" aria-hidden="true">
		{#if art}
			{#if sprite}
				<AvatarSprite url={assetUrl(art)} size={96} />
			{:else}
				<img class="category-image" src={assetUrl(art)} alt="" />
			{/if}
		{/if}
	</span>
	<span class="category-title">{label}</span>
</button>

<style>
	.category-tile {
		position: relative;
		display: block;
		width: 100%;
		min-height: 9rem;
		padding: 0;
		overflow: hidden;
		background: var(--surface-deep);
		color: var(--text-h);
		cursor: pointer;
		--pc-fill: var(--surface-deep);
	}

	.category-tile:hover,
	.category-tile:focus-visible {
		--pc-border: var(--accent);
	}

	.category-art {
		position: absolute;
		inset: 0;
		display: grid;
		place-items: center;
		opacity: 0.85;
	}

	.category-image {
		width: 100%;
		height: 100%;
		object-fit: cover;
		image-rendering: pixelated;
	}

	.category-title {
		position: absolute;
		bottom: 0.5rem;
		left: 0.5rem;
		padding: 0.2rem 0.5rem;
		background: var(--surface);
		font-family: var(--pypx);
		font-weight: 800;
		letter-spacing: 0.08em;
		text-transform: uppercase;
	}
</style>
