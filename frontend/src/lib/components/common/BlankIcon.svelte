<!-- The Blank currency icon: a small purple blank card in pixel art. With a
     label it is an image with that name and tooltip; without one it is
     decoration next to text that already names the currency. -->
<script lang="ts">
	/** Width to height ratio of a blank card (0.6em by 0.85em). */
	const BLANK_CARD_RATIO = 0.6 / 0.85;
	const EM_PRECISION = 1000;

	let { size = 0.85, label }: { size?: number; label?: string } = $props();

	const width = $derived(Math.round(size * BLANK_CARD_RATIO * EM_PRECISION) / EM_PRECISION);
	const box = $derived(`width: ${width}em; height: ${size}em`);
</script>

{#if label}
	<span class="blank-icon" role="img" aria-label={label} title={label} style={box}></span>
{:else}
	<span class="blank-icon" aria-hidden="true" style={box}></span>
{/if}

<style>
	.blank-icon {
		display: inline-block;
		flex: 0 0 auto;
		box-sizing: border-box;
		border: 1px solid color-mix(in srgb, var(--accent) 55%, black);
		border-radius: 0;
		background: var(--accent);
		box-shadow: inset 1px 1px 0 var(--accent-h);
		vertical-align: middle;
	}
</style>
