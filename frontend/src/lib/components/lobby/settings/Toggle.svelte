<script lang="ts">
	import Tooltip from "$components/common/Tooltip.svelte";
	import RichText from "$components/common/RichText.svelte";
	import PixelCheckbox from "$components/common/PixelCheckbox.svelte";

	let {
		label,
		description,
		checked,
		disabled = false,
		oncommit
	}: {
		label: string;
		description?: string;
		checked: boolean;
		disabled?: boolean;
		oncommit: (value: boolean) => void;
	} = $props();
</script>

{#snippet toggle()}
	<PixelCheckbox {label} {checked} {disabled} {oncommit} />
{/snippet}

{#if (description?.length ?? 0) > 0}
	<Tooltip interactive={true} title={label}>
		{#snippet tooltipContent()}
			<RichText text={description ?? ""} allowKeywords={true} />
		{/snippet}
		{@render toggle()}
	</Tooltip>
{:else}
	{@render toggle()}
{/if}
