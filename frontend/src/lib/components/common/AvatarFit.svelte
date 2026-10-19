<!-- An avatar that fills its box at the largest whole pixel scale. -->
<script lang="ts">
	import Avatar from "$components/common/Avatar.svelte";
	import { fitSpriteSize } from "$lib/avatar/pixelScale";
	import type { Loadout } from "$lib/avatar/types";

	let { loadout = null, bot = false }: { loadout?: Loadout | null; bot?: boolean } = $props();

	let width = $state(0);
	let height = $state(0);
	const size = $derived(fitSpriteSize(Math.min(width, height)));
</script>

<div class="avatar-fit" bind:clientWidth={width} bind:clientHeight={height} data-size={size}>
	<Avatar {loadout} {bot} {size} />
</div>

<style>
	.avatar-fit {
		display: grid;
		place-items: center;
		width: 100%;
		height: 100%;
		min-width: 0;
		min-height: 0;
		overflow: hidden;
	}
</style>
