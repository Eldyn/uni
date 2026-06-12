<script module>
	export type EnumOption<T> = {
		value: T;
		label: string;
		description: string;
	};
</script>

<script lang="ts" generics="T">
	import Tooltip from "../../common/Tooltip.svelte";

	let {
		label,
		description,
		value,
		options,
		disabled = false,
		oncommit
	}: {
		label: string;
		description?: string;
		value: T;
		options: EnumOption<T>[];
		disabled?: boolean;
		oncommit: (value: T) => void;
	} = $props();
</script>

{#snippet selectorControl()}
	<label class="selector-label" class:disabled>
		<span>{label}</span>
		<select
			class="enum-selector"
			{disabled}
			onchange={(e) => {
				const target = e.target as HTMLSelectElement;
				oncommit(options[target.selectedIndex].value);
			}}
		>
			{#each options as option}
				<option value={String(option.value)} selected={option.value === value}>
					{option.label}
				</option>
			{/each}
		</select>
	</label>
{/snippet}

{#if (description?.length ?? 0) > 0}
	<Tooltip>
		{#snippet tooltipContent()}
			<span>{description}</span>
		{/snippet}
		{@render selectorControl()}
	</Tooltip>
{/if}

<style>
	.selector-label {
		display: flex;
		align-items: center;
		justify-content: space-between; /* Pushes the select box to the right */
		gap: 16px;
		font-size: 14px;
		font-weight: 500;
		color: var(--text-h);
		user-select: none;
	}

	.selector-label.disabled {
		cursor: not-allowed;
		opacity: 0.6;
	}

	select {
		padding: 4px 8px;
		border-radius: 4px;
		border: 1px solid var(--border-color, #ccc);
		background: var(--bg-color, #fff);
		color: var(--text-h);
		font-size: 13px;
		cursor: pointer;
		outline: none;
		font-family: "Pixel";
		font-size: 10px;
		font-weight: 500;
	}

	select:disabled {
		cursor: not-allowed;
	}

	.enum-selector {
		color: var(--text);
		background-color: var(--bg);
	}
</style>
