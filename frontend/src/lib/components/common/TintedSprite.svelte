<script lang="ts">
	type SpriteSize = 16 | 32 | 48 | 64 | 96;

	let {
		src,
		color,
		// Sizing for both the mask and the blended background. Mirrors the
		// per-site mask-size the masked elements used before (e.g. "100% 100%"
		// to stretch, "contain" to fit, "cover" by default).
		size = 32,
		fit = "cover"
	}: { src: string; color: string; size?: SpriteSize; fit?: string } = $props();
</script>

<div
	class="tinted-sprite"
	style="
		--sprite-img: url('{src}');
		--sprite-color: {color};
		--sprite-fit: {fit};
		width: {size}px;
		height: {size}px;
	"
></div>

<style>
	.tinted-sprite {
		/* Sized by the `size` prop (see the component script) to one of a fixed
		   set of integer steps — pixel art scales by whole multiples only.
		   image-rendering is set explicitly because the global `img { ... }`
		   rule in app.css does not reach a masked element. crisp-edges is
		   declared first as a fallback so the more specific `pixelated`
		   keyword — which wins the cascade when both are recognized — is
		   the one actually applied. */
		image-rendering: crisp-edges;
		image-rendering: pixelated;

		/* Presentation & Blend engine */
		background-color: var(--sprite-color);
		background-image: var(--sprite-img);
		background-blend-mode: multiply;

		/* Masking */
		-webkit-mask-image: var(--sprite-img);
		mask-image: var(--sprite-img);

		/* Adaptive Scaling */
		background-size: var(--sprite-fit, cover);
		background-position: center;
		-webkit-mask-size: var(--sprite-fit, cover);
		mask-size: var(--sprite-fit, cover);
		-webkit-mask-position: center;
		mask-position: center;
		-webkit-mask-repeat: no-repeat;
		mask-repeat: no-repeat;
	}
</style>
