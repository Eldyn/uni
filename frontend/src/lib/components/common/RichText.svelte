<script lang="ts">
	import TextEffects from "$components/common/TextEffects.svelte";
	import { parseRichText } from "$utils/richText";
	import { censorText, loadCensorData } from "$utils/censor.svelte";
	import { storeTooltipStack } from "$stores/tooltipStack.svelte";
	import { hasGlossaryKeyword } from "$lib/glossary/glossary";

	let {
		text,
		class: className = "",
		allowKeywords = false,
		tooltipParentId = null,
		censor = false
	}: {
		text: string;
		class?: string;
		allowKeywords?: boolean;
		tooltipParentId?: string | null;
		censor?: boolean;
	} = $props();

	// Kicks off lazy load of profanity word data only when censoring is enabled
	$effect(() => {
		if (censor) {
			loadCensorData();
		}
	});

	const segments = $derived(parseRichText(text, { allowKeywords }));

	function handleKeywordClick(e: MouseEvent, keyword: string) {
		e.preventDefault();
		e.stopPropagation();
		storeTooltipStack.open(keyword, e.currentTarget as HTMLElement, tooltipParentId);
	}
</script>

<span class={className}>
	{#each segments as segment, i (i)}
		{@const displayText = censor ? censorText(segment.text) : segment.text}
		{#if segment.keyword && hasGlossaryKeyword(segment.keyword)}
			<button
				type="button"
				class="glossary-keyword-btn"
				onclick={(e) => handleKeywordClick(e, segment.keyword!)}
				style="{segment.bold ? 'font-weight: 800;' : ''}{segment.italic
					? 'font-style: italic;'
					: ''}{segment.color ? `color: ${segment.color};` : ''}"
			>
				{displayText}
			</button>
		{:else if segment.effect}
			<TextEffects
				text={displayText}
				effect={segment.effect}
				color={segment.color ?? ""}
			/>
		{:else}
			<!-- Chat text inherits TinyUnicode (see ChatLog/ChatComposer) for near-
			     full unicode coverage; bold/italic apply as plain CSS instead of
			     switching to Pypx/Monogram, which don't cover most alphabets and
			     would break rendering for players typing in other languages. -->
			<span
				style="{segment.bold ? 'font-weight: 800;' : ''}{segment.italic
					? 'font-style: italic;'
					: ''}{segment.color ? `color: ${segment.color};` : ''}"
			>
				{displayText}
			</span>
		{/if}
	{/each}
</span>

<style>
	.glossary-keyword-btn {
		display: inline;
		pointer-events: auto;
		background: none;
		border: none;
		padding: 0;
		margin: 0;
		font: inherit;
		color: var(--brand, #38bdf8);
		text-decoration: underline;
		text-decoration-style: dotted;
		text-underline-offset: 2px;
		cursor: pointer;
		line-height: inherit;
		transition: color 0.15s ease;
	}

	.glossary-keyword-btn:hover {
		color: var(--brand-2, #818cf8);
		text-decoration-style: solid;
	}

	.glossary-keyword-btn:focus-visible {
		outline: 2px solid var(--brand, #38bdf8);
		outline-offset: 1px;
	}
</style>
