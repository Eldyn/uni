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
	import Listbox from "$components/common/Listbox.svelte";
	import * as m from "$lib/paraglide/messages.js";
	import { storeI18n } from "$stores/i18n.svelte";

	let {
		label,
		description,
		value,
		options,
		disabled = false,
		extraClass = "",
		oncommit
	}: {
		label: string;
		description?: string;
		value: T;
		options: EnumOption<T>[];
		disabled?: boolean;
		extraClass?: string;
		oncommit: (value: T) => void;
	} = $props();

	const id = `enum-${Math.random().toString(36).slice(2, 9)}`;

	let hasFieldDescription = $derived((description?.length ?? 0) > 0);
	let selectedOption = $derived(options.find((option) => option.value === value));
	let hasSelectionDescription = $derived((selectedOption?.description?.length ?? 0) > 0);
	let optionValues = $derived(options.map((option) => option.value));
	let optionByValue = $derived(new Map(options.map((option) => [option.value, option] as const)));
</script>

{#snippet trigger()}
	<span class="sr-only">{label}</span>
	<span class="enum-trigger-label">{selectedOption?.label ?? ""}</span>
{/snippet}

{#snippet option(v: T)}
	{@const opt = optionByValue.get(v)}
	<span class="enum-option">
		<span class="enum-option__label">{opt?.label}</span>
		{#if opt?.description}
			<span class="enum-option__desc">{opt.description}</span>
		{/if}
	</span>
{/snippet}

{#snippet selectControl()}
	<div class="enum-listbox {extraClass}">
		<Listbox
			{id}
			{label}
			options={optionValues}
			selected={value}
			{disabled}
			onselect={(v) => oncommit(v)}
			{trigger}
			{option}
		/>
	</div>
{/snippet}

{#snippet selectorControl()}
	<div class="selector-label" class:disabled>
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
	</div>
{/snippet}

{@render selectorControl()}

<style>
	.selector-label {
		display: flex;
		align-items: center;
		gap: var(--space-3);
		font-family: var(--tiny);
		font-size: 15px;
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
		clip-path: none !important;
	}

	.info-btn:hover {
		color: var(--text-h);
	}

	/* Listbox paints its own notched trigger/popup; square them off here so the
	   enum control matches the plain-square chips and checkboxes. */
	.enum-listbox {
		--notch-clip: none;
	}

	.enum-listbox :global(button[aria-haspopup="listbox"]) {
		font-family: var(--tiny);
		font-size: 15px;
		color: var(--text-h);
	}

	.enum-listbox :global([role="listbox"]) {
		min-width: 100%;
	}

	.enum-option {
		display: flex;
		flex-direction: column;
		gap: 2px;
		text-align: left;
	}

	.enum-option__label {
		font-family: var(--tiny);
		font-size: 15px;
		color: var(--text-h);
	}

	.enum-option__desc {
		font-family: var(--tiny);
		font-size: 13px;
		color: var(--text);
	}

	/* Tooltip wraps a single control here, so its default full-width
	   inline-block container must not stretch or break the label row. */
	:global(.tooltip-container.enum-info-tooltip),
	:global(.tooltip-container.enum-select-tooltip) {
		display: inline-flex;
		width: auto;
	}
</style>
