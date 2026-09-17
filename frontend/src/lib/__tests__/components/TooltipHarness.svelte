<script lang="ts">
	import Tooltip from "$components/common/Tooltip.svelte";
	import type { Side } from "$lib/utils/anchorPlacement";

	let {
		side = "top",
		openDelay = 0,
		closeDelay = 150,
		interactive = false
	}: {
		side?: Side;
		openDelay?: number;
		closeDelay?: number;
		interactive?: boolean;
	} = $props();
</script>

<Tooltip {side} {openDelay} {closeDelay} {interactive}>
	{#snippet tooltipContent()}
		<span>Popover description text</span>
	{/snippet}
	<button type="button">Trigger Button</button>
</Tooltip>
