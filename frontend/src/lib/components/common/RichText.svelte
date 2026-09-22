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
		const target = e.currentTarget as HTMLElement;
		// A keyword inside a stacked glossary tooltip already has a real parent
		// entry (tooltipParentId) to anchor against, so keep the keyword itself
		// as the trigger. A keyword in an unparented card (the root hover
		// popover, or the card-detail popover) has no stack parent: anchoring
		// to the keyword would drop the child on top of the card's own text.
		// Anchor to the enclosing card box instead, so the depth-0 placement
		// puts the child outside (above/beside) it.
		const anchor = tooltipParentId
			? target
			: (target.closest<HTMLElement>(".tooltip-card") ?? target);
		// Propagate the enclosing tooltip's link color to the child tooltip, so
		// a card-colored tooltip's links stay card-colored all the way down.
		// Falls back to undefined (store inherits the parent entry's color).
		const linkColor =
			getComputedStyle(target).getPropertyValue("--tooltip-link-color").trim() || undefined;
		storeTooltipStack.open(keyword, anchor, tooltipParentId, linkColor);
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
						color={segment.color ?? "var(--tooltip-link-color, var(--redCard, #bd3130))"}
						shineBaseColor={segment.color ?? "var(--tooltip-link-color, var(--redCard, #bd3130))"}
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
		/* app.css clips every `button:not(.pixel-bordered)` to the pixel-corner
		   notch shape by default; this is an inline text link, not a chrome
		   button, and that clip was cutting into glyph ascenders/descenders at
		   its (tight, text-sized) corners. Opt out like .text-link-inline does. */
		clip-path: none !important;
		border-radius: 0 !important;
		display: inline;
		pointer-events: auto;
		background: none;
		border: none;
		padding: 0;
		margin: 0;
		font: inherit;
		font-weight: 700;
		color: var(--tooltip-link-color, var(--redCard, #bd3130));
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
		outline: 2px solid var(--tooltip-link-color, var(--redCard, #bd3130));
		outline-offset: 1px;
	}
</style>
