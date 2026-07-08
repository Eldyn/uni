<script lang="ts">
	import Tooltip from "$components/common/Tooltip.svelte";

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
	<label class="toggle-label" class:disabled>
		<input
			type="checkbox"
			{checked}
			{disabled}
			onchange={(e) => oncommit((e.target as HTMLInputElement).checked)}
		/>
		<span>{label}</span>
	</label>
{/snippet}

{#if (description?.length ?? 0) > 0}
	<Tooltip>
		{#snippet tooltipContent()}
			<span>{description}</span>
		{/snippet}
		{@render toggle()}
	</Tooltip>
{:else}
	{@render toggle()}
{/if}

<style>
	.toggle-label {
		display: flex;
		align-items: center;
		gap: 8px;
		font-size: 14px;
		font-weight: 500;
		color: var(--text-h);
		cursor: pointer;
		user-select: none;
	}

	.toggle-label.disabled {
		cursor: not-allowed;
		opacity: 0.6;
	}

	.toggle-label input:disabled + span {
		cursor: not-allowed;
	}
</style>
