<!-- frontend/src/lib/components/game/SpectatorFade.svelte -->
<!-- Full-screen black used to cover a spectator POV switch: the screen darkens,
     storeSpectator swaps the viewed player at the darkest point, then this
     fades back out onto the new view. Always rendered at the top of the stack
     (above the HUD and banners) so the whole screen, not just the board,
     transitions. -->
<script lang="ts">
	import { storeSpectator } from "$stores/spectator.svelte";
</script>

{#if storeSpectator.fadeOpacity > 0}
	<!-- svelte-ignore a11y_no_static_element_interactions -->
	<div
		class="spectator-fade"
		class:active={storeSpectator.transitioning}
		style="opacity: {storeSpectator.fadeOpacity};"
		aria-hidden="true"
	></div>
{/if}

<style>
	.spectator-fade {
		position: fixed;
		inset: 0;
		z-index: 1000;
		background: #000;
		/* Blocks a second POV click (and any other board input) while the
		   screen is covered, so switches can't stack. */
		pointer-events: none;
	}

	.spectator-fade.active {
		pointer-events: auto;
	}
</style>
