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
				{#if segment.effect}
					<TextEffects
						text={displayText}
						effect={segment.effect}
						color={segment.color ?? "var(--redCard, #bd3130)"}
						class="fx-{segment.effect}"
					/>
				{:else}
					{displayText}
				{/if}
			</button>
		{:else if segment.effect}
			<TextEffects
				text={displayText}
				effect={segment.effect}
				color={segment.color ?? ""}
				class="fx-{segment.effect}"
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
		font-weight: 700;
		color: var(--redCard, #bd3130);
		text-decoration: underline;
		text-decoration-style: solid;
		text-underline-offset: 2px;
		cursor: pointer;
		line-height: inherit;
		transition: opacity 0.15s ease;
	}

	.glossary-keyword-btn:hover {
		opacity: 0.8;
	}

	.glossary-keyword-btn:focus-visible {
		outline: 2px solid var(--redCard, #bd3130);
		outline-offset: 1px;
	}
</style>
