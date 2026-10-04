<script lang="ts">
	import TintedSprite from "$components/common/TintedSprite.svelte";
	import { assetUrl } from "$lib/utils/assetUrl";
	import type { LoaderFace } from "./loaderFaces";

	let { face }: { face: LoaderFace } = $props();

	const COLOR_MAP: Record<string, string> = {
		red: "var(--redCard)",
		blue: "var(--blueCard)",
		green: "var(--greenCard)",
		yellow: "var(--yellowCard)",
		white: "var(--whiteCard)"
	};

	const cardColor = $derived(COLOR_MAP[face.type] ?? "inherit");
	const tinted = $derived(face.value !== "jolly");
	const faceSrc = $derived(assetUrl(`/assets/cards/${face.value}.png`));
</script>

<div class="loader-card" data-testid="loader-card" style="--card-color: {cardColor};">
	<div class="side front">
		<img src="/assets/cards/background.png" alt="" class="layer" />
		{#if tinted}
			<div class="layer">
				<TintedSprite src={faceSrc} color="var(--card-color)" fit="100% 100%" />
			</div>
			<div class="layer">
				<TintedSprite src="/assets/cards/border.png" color="var(--card-color)" fit="100% 100%" />
			</div>
		{:else}
			<img src={faceSrc} alt="" class="layer" />
			<img src="/assets/cards/border.png" alt="" class="layer" />
		{/if}
	</div>
	<div class="side back">
		<img src="/assets/cards/back.png" alt="" class="layer" />
	</div>
</div>

<style>
	.loader-card {
		position: relative;
		width: var(--loader-card-width);
		height: calc(var(--loader-card-width) * 1.5357);
		transform-style: preserve-3d;
	}
	.side {
		position: absolute;
		inset: 0;
		border-radius: 0.8em;
		overflow: hidden;
		backface-visibility: hidden;
	}
	.back {
		transform: rotateY(180deg);
	}
	.layer {
		position: absolute;
		inset: 0;
		width: 100%;
		height: 100%;
		image-rendering: pixelated;
	}
	/* TintedSprite defaults to a fixed 32px icon box; the card art must
	   full-bleed its layer instead (mirrors GameCard.svelte's override). */
	.layer :global(.tinted-sprite) {
		width: 100% !important;
		height: 100% !important;
	}
</style>
