<!-- The rarity frame shared by the shop's item card and detail panel: a pixel
     border in the rarity colour, an animated edge shimmer for rare and above,
     and a slow rotating sunburst for epic. Both animations are CSS only and
     stop under `prefers-reduced-motion`. -->
<script lang="ts">
	import type { Snippet } from "svelte";
	import type { Rarity } from "$lib/avatar/types";
	import { RARITY_COLOURS } from "$lib/shop/rarity";

	let {
		rarity,
		class: className = "",
		children
	}: { rarity: Rarity; class?: string; children: Snippet } = $props();

	const colour = $derived(RARITY_COLOURS[rarity]);
	const shimmer = $derived(rarity === "rare" || rarity === "epic");
	const sunburst = $derived(rarity === "epic");
</script>

<div
	class="rarity-frame {className}"
	data-rarity={rarity}
	data-shimmer={shimmer ? "true" : "false"}
	style="--rarity-colour: {colour}"
>
	{#if sunburst}
		<span class="rarity-sunburst" aria-hidden="true"></span>
	{/if}
	<div class="rarity-body">{@render children()}</div>
	{#if shimmer}
		<span class="rarity-shimmer" aria-hidden="true"></span>
	{/if}
</div>

<style>
	.rarity-frame {
		position: relative;
		display: flex;
		border: 2px solid var(--rarity-colour);
		background: var(--surface-deep);
		overflow: hidden;
		isolation: isolate;
	}

	.rarity-body {
		position: relative;
		z-index: 1;
		display: flex;
		flex: 1;
		flex-direction: column;
		min-width: 0;
	}

	/* A highlight sweeping around the frame's inner edge. */
	.rarity-shimmer {
		position: absolute;
		inset: -2px;
		z-index: 2;
		pointer-events: none;
		border: 2px solid transparent;
		background: linear-gradient(
				115deg,
				transparent 35%,
				rgba(255, 255, 255, 0.9) 50%,
				transparent 65%
			)
			border-box;
		background-size: 250% 250%;
		-webkit-mask:
			linear-gradient(#000 0 0) padding-box,
			linear-gradient(#000 0 0);
		-webkit-mask-composite: xor;
		mask-composite: exclude;
		animation: rarity-shimmer 2.6s linear infinite;
	}

	/* Kept subtle so the art stays readable; sits behind the body. */
	.rarity-sunburst {
		position: absolute;
		inset: -50%;
		z-index: 0;
		pointer-events: none;
		opacity: 0.5;
		background: repeating-conic-gradient(
			from 0deg,
			color-mix(in srgb, var(--rarity-colour) 45%, transparent) 0deg 6deg,
			transparent 6deg 18deg
		);
		animation: rarity-sunburst 18s linear infinite;
	}

	@keyframes rarity-shimmer {
		from {
			background-position: 200% 0;
		}
		to {
			background-position: -100% 0;
		}
	}

	@keyframes rarity-sunburst {
		to {
			transform: rotate(360deg);
		}
	}

	@media (prefers-reduced-motion: reduce) {
		.rarity-shimmer {
			display: none;
		}
		.rarity-sunburst {
			animation: none;
		}
	}
</style>
