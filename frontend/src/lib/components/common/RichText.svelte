<script lang="ts">
	import TextEffects from "$components/common/TextEffects.svelte";
	import { parseRichText, fxRenderProps } from "$utils/richText";
	import { censorText, loadCensorData } from "$utils/censor.svelte";
	import { storeTooltipStack } from "$stores/tooltipStack.svelte";
	import { hasGlossaryKeyword } from "$lib/glossary/glossary";
	import { storeAnimation } from "$stores/animation.svelte";
	import { storeWebglCapability } from "$stores/webglCapability.svelte";

	let {
		text,
		class: className = "",
		allowKeywords = false,
		tooltipParentId = null,
		censor = false,
		logTags = false
	}: {
		text: string;
		class?: string;
		allowKeywords?: boolean;
		tooltipParentId?: string | null;
		censor?: boolean;
		/** Trusted log lines only: named palette colours and escapes; never set for player chat. */
		logTags?: boolean;
	} = $props();

	// Kicks off lazy load of profanity word data only when censoring is enabled
	$effect(() => {
		if (censor) {
			loadCensorData();
		}
	});

	const segments = $derived(
		parseRichText(text, { allowKeywords, allowLogTags: logTags }).map((segment) =>
			segment.effect === "shake" && (!storeAnimation.enabled || storeWebglCapability.reducedMotion)
				? { ...segment, effect: undefined }
				: segment
		)
	);

	function segmentInlineStyle(segment: {
		bold?: boolean;
		italic?: boolean;
		color?: string | null;
	}): string {
		const parts: string[] = [];
		const styled = segment.bold || segment.italic;
		// Monogram's cap-height is ~0.44em vs ~1em for the Pypx and Monogram-italic
		// faces, so bold/italic must be scaled down to match a Monogram body.
		// --rt-sib-scale is set by Monogram-bodied callers (chat) and defaults to 1
		// so every other RichText context keeps its base size.
		if (styled) parts.push("font-size: calc(var(--rt-sib-scale, 1) * 1em);");
		if (segment.bold) {
			parts.push("font-family: var(--pypx);", "font-weight: 700;");
		} else if (segment.italic) {
			parts.push("font-family: var(--monogram);");
		}
		if (segment.italic) parts.push("font-style: italic;");
		if (segment.color) parts.push(`color: ${segment.color};`);
		return parts.join("");
	}

	function segmentFont(segment: { bold?: boolean; italic?: boolean }): string {
		if (segment.bold) return "var(--pypx)";
		if (segment.italic) return "var(--monogram)";
		return "";
	}

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
				style={segmentInlineStyle(segment)}
			>
				{#if segment.effect}
					<TextEffects
						text={displayText}
						effect={segment.effect}
						color={segment.color ?? "var(--tooltip-link-color, var(--redCard, #bd3130))"}
						shineBaseColor={segment.color ?? "var(--tooltip-link-color, var(--redCard, #bd3130))"}
						font={segmentFont(segment)}
						class="fx-{segment.effect} {segment.bold || segment.italic ? 'rt-scaled' : ''}"
						{...fxRenderProps(segment.effect, segment.effectIntensity, segment.effectSpeed)}
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
				font={segmentFont(segment)}
				class="fx-{segment.effect} {segment.bold || segment.italic ? 'rt-scaled' : ''}"
				{...fxRenderProps(segment.effect, segment.effectIntensity, segment.effectSpeed)}
			/>
		{:else}
			<!-- Bold uses the Pypx bold face and italic the Monogram-italic face,
			     each scaled by --rt-sib-scale so they match a Monogram body. -->
			<span style={segmentInlineStyle(segment)}>
				{displayText}
			</span>
		{/if}
	{/each}
</span>

<style>
	/* TextEffects wrappers for bold/italic segments get the same size
	   compensation as the plain-span path. */
	:global(.rt-scaled) {
		font-size: calc(var(--rt-sib-scale, 1) * 1em);
	}

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
