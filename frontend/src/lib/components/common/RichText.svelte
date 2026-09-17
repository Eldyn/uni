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
		tooltipParentId = null
	}: {
		text: string;
		class?: string;
		allowKeywords?: boolean;
		tooltipParentId?: string | null;
	} = $props();

	// Kicks off the (idempotent) lazy load of the profanity word data on the
	// first RichText anywhere, cheap to call from every instance, since
	// loadCensorData() only actually fetches once.
	loadCensorData();

	const segments = $derived(parseRichText(text, { allowKeywords }));

	function handleKeywordClick(e: MouseEvent, keyword: string) {
		e.preventDefault();
		e.stopPropagation();
		storeTooltipStack.open(keyword, e.currentTarget as HTMLElement, tooltipParentId);
	}
</script>

<span class={className}>
	{#each segments as segment, i (i)}
		{#if segment.keyword && hasGlossaryKeyword(segment.keyword)}
			<button
				type="button"
				class="glossary-keyword-btn"
				onclick={(e) => handleKeywordClick(e, segment.keyword!)}
				style="{segment.bold ? 'font-weight: 800;' : ''}{segment.italic
					? 'font-style: italic;'
					: ''}{segment.color ? `color: ${segment.color};` : ''}"
			>
				{censorText(segment.text)}
			</button>
		{:else if segment.effect}
			<TextEffects
				text={censorText(segment.text)}
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
				{censorText(segment.text)}
			</span>
		{/if}
	{/each}
</span>

<style>
	.glossary-keyword-btn {
		display: inline;
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
