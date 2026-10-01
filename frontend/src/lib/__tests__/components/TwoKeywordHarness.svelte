<script lang="ts">
	import Tooltip from "$components/common/Tooltip.svelte";
	import RichText from "$components/common/RichText.svelte";

	let {
		openDelay = 0,
		closeDelay = 150,
		interactive = true
	}: {
		openDelay?: number;
		closeDelay?: number;
		interactive?: boolean;
	} = $props();
</script>

<Tooltip {openDelay} {closeDelay} {interactive} title="Turn">
	{#snippet tooltipContent()}
		<RichText
			text="A player's opportunity to [k=vanilla:play]play[/k] a valid card or [k=vanilla:draw]draw[/k]."
			allowKeywords={true}
		/>
	{/snippet}
	<button type="button">Trigger Button</button>
</Tooltip>
