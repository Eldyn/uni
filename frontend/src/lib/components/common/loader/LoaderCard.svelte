<script lang="ts">
	import TintedSprite from "$components/common/TintedSprite.svelte";
	import { cardBackUrl, cardFaceUrl } from "$lib/cards/skinPaths";
	import { storeCosmetics } from "$stores/cosmetics.svelte";
	import type { LoaderFace } from "./loaderFaces";

	let { face, color }: { face: LoaderFace; color?: string } = $props();

	const COLOR_MAP: Record<string, string> = {
		red: "var(--redCard)",
		blue: "var(--blueCard)",
		green: "var(--greenCard)",
		yellow: "var(--yellowCard)",
		white: "var(--whiteCard)"
	};

	const cardColor = $derived(color ?? COLOR_MAP[face.type] ?? "inherit");
	const tinted = $derived(face.value !== "jolly");
	const baseFaceSrc = $derived(cardFaceUrl(face.value, null));
	const skinFaceSrc = $derived(cardFaceUrl(face.value, storeCosmetics.slots.card_face));
	let failedFaceSrc = $state<string | null>(null);
	const faceSrc = $derived(failedFaceSrc === skinFaceSrc ? baseFaceSrc : skinFaceSrc);
	const backSrc = $derived(cardBackUrl(storeCosmetics.slots.card_back));
	const baseBackSrc = cardBackUrl(null);
	let failedBackSrc = $state<string | null>(null);
	const displayBackSrc = $derived(failedBackSrc === backSrc ? baseBackSrc : backSrc);
</script>

<div class="loader-card" data-testid="loader-card" style="--card-color: {cardColor};">
	<div class="side front">
		<img src="/assets/cards/background.png" alt="" class="layer" />
		{#if skinFaceSrc !== baseFaceSrc && failedFaceSrc !== skinFaceSrc}
			<img
				src={skinFaceSrc}
				alt=""
				class="skin-probe"
				aria-hidden="true"
				data-testid="loader-card-face-probe"
				onerror={() => (failedFaceSrc = skinFaceSrc)}
			/>
		{/if}
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
		<img
			src={displayBackSrc}
			alt=""
			class="layer"
			data-testid="loader-card-back"
			onerror={() => (failedBackSrc = backSrc)}
		/>
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
	.skin-probe {
		display: none;
	}
	/* TintedSprite defaults to a fixed 32px icon box; the card art must
	   full-bleed its layer instead (mirrors GameCard.svelte's override). */
	.layer :global(.tinted-sprite) {
		width: 100% !important;
		height: 100% !important;
	}
</style>
