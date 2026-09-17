<script lang="ts">
	import Tooltip from "$components/common/Tooltip.svelte";
	import type { Side } from "$lib/utils/anchorPlacement";

	let {
		side = "top",
		openDelay = 0
	}: {
		side?: Side;
		openDelay?: number;
	} = $props();
</script>

<Tooltip {side} {openDelay}>
	{#snippet tooltipContent()}
		<span>Popover description text</span>
	{/snippet}
	<button type="button">Trigger Button</button>
</Tooltip>
