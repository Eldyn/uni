<script module>
	export type EnumOption<T> = {
		value: T;
		label: string;
		description?: string;
	};
</script>

<script lang="ts" generics="T">
	import Tooltip from "$components/common/Tooltip.svelte";
	import RichText from "$components/common/RichText.svelte";
	import * as m from "$lib/paraglide/messages.js";
	import { storeI18n } from "$stores/i18n.svelte";

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

	let hasFieldDescription = $derived((description?.length ?? 0) > 0);
	let selectedOption = $derived(options.find((option) => option.value === value));
	let hasSelectionDescription = $derived((selectedOption?.description?.length ?? 0) > 0);
</script>

{#snippet selectorControl()}
	<label class="selector-label" class:disabled>
		{#if hasFieldDescription}
			<Tooltip interactive={true} title={label} class="enum-info-tooltip">
				{#snippet tooltipContent()}
					<RichText text={description ?? ""} allowKeywords={true} />
				{/snippet}
				<button
					type="button"
					class="info-btn"
					aria-label={m.common_more_info({}, { locale: storeI18n.locale })}
				>
					<i class="pia pixelart-icons-font-circle-info"></i>
				</button>
			</Tooltip>
		{/if}
		<span class="selector-label-text">{label}</span>
		{#if hasSelectionDescription}
			<Tooltip
				interactive={true}
				title={selectedOption?.label ?? label}
				class="enum-select-tooltip"
			>
				{#snippet tooltipContent()}
					<RichText text={selectedOption?.description ?? ""} allowKeywords={true} />
				{/snippet}
				{@render selectControl()}
			</Tooltip>
		{:else}
			{@render selectControl()}
		{/if}
	</label>
{/snippet}

{#snippet selectControl()}
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
{/snippet}

{@render selectorControl()}

<style>
	.selector-label {
		display: flex;
		align-items: center;
		gap: 8px;
		font-size: 14px;
		font-weight: 500;
		color: var(--text-h);
		user-select: none;
	}

	.selector-label.disabled {
		cursor: not-allowed;
		opacity: 0.6;
	}

	.selector-label-text {
		flex: 1;
	}

	.info-btn {
		display: inline-flex;
		align-items: center;
		padding: 0;
		border: none;
		background: none;
		color: var(--text);
		cursor: help;
		font-size: 18px;
		line-height: 1;
	}

	.info-btn:hover {
		color: var(--text-h);
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

	/* Tooltip wraps a single control here, so its default full-width
	   inline-block container must not stretch or break the label row. */
	:global(.tooltip-container.enum-info-tooltip),
	:global(.tooltip-container.enum-select-tooltip) {
		display: inline-flex;
		width: auto;
	}
</style>
