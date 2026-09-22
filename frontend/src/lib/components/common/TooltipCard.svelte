<script lang="ts">
	import type { Snippet } from "svelte";
	import type { GlossaryTag } from "$lib/glossary/glossary";

	interface Props {
		title?: string;
		tags?: GlossaryTag[];
		children?: Snippet;
		onclose?: () => void;
		/** Color for glossary hyperlinks in the tooltip content. Inherited by
		 *  any nested RichText via the --tooltip-link-color custom property.
		 *  Defaults to gold; card tooltips pass the card's own color. */
		linkColor?: string;
	}

	let { title, tags = [], children, onclose, linkColor = "var(--gold, #ffcc00)" }: Props = $props();

	function handleCardClick(e: MouseEvent) {
		const target = e.target as HTMLElement | null;
		if (target?.closest("button, a, input, textarea")) return;
		onclose?.();
	}
</script>

<!-- svelte-ignore a11y_click_events_have_key_events -->
<!-- svelte-ignore a11y_no_static_element_interactions -->
<div
	class="tooltip-card pixel-bordered"
	style:--tooltip-link-color={linkColor}
	onclick={handleCardClick}
>
	{#if title}
		<div class="tooltip-title">{title}</div>
	{/if}

	<div class="tooltip-content-box pixel-bordered">
		{@render children?.()}
	</div>

	{#if tags.length > 0}
		<div class="tooltip-tags-row">
			{#each tags as tag}
				<span
					class="tooltip-tag-pill"
					style:background={tag.bg}
					style:--pill-shadow={tag.shadowColor ?? "rgba(0, 0, 0, 0.4)"}
					style:color={tag.textColor ?? "#ffffff"}
				>
					{tag.label}
				</span>
			{/each}
		</div>
	{/if}
</div>

<style>
	/*
	 * The south-facing "hard offset" shadow can't be a box-shadow on the main
	 * element: .pixel-bordered only clips its ::before/::after pseudo layers
	 * (see app.css), never the element itself, so a box-shadow here would
	 * render as a plain rectangle underneath the notched card and mismatch at
	 * every corner. filter: drop-shadow() shadows the actual painted alpha
	 * (the clipped pseudo layers included), so it follows the notch shape.
	 */
	.tooltip-card {
		--pc-fill: var(--surface);
		--pc-border: var(--border);
		--pc-width: 2px;
		filter: drop-shadow(0 4px 0 var(--pixel-shadow));
		padding: 8px 10px;
		display: flex;
		flex-direction: column;
		gap: 6px;
		max-width: 300px;
		width: max-content;
		user-select: none;
		cursor: pointer;
	}

	.tooltip-title {
		text-align: center;
		color: var(--text-h);
		font-family: var(--pixel);
		font-weight: 400;
		font-size: 0.95rem;
		line-height: 1.3;
		letter-spacing: 0.04em;
		text-shadow: 1px 1px 0 var(--pixel-shadow);
	}

	/*
	 * Recessed "hole", not a raised tile: no south drop-shadow (the outer
	 * card keeps its own), clip the element itself to the notch so the fill
	 * reads flush with the outer card, and bevel with dark top/left + light
	 * bottom/right inset edges (classic pixel inset).
	 */
	.tooltip-content-box {
		--pc-fill: var(--surface-deep);
		--pc-border: var(--border);
		--pc-width: 2px;
		clip-path: var(--notch-clip);
		box-shadow:
			inset 2px 2px 0 var(--pixel-shadow),
			inset -2px -2px 0 var(--border);
		padding: 8px 10px;
		color: var(--text-h);
		font-family: var(--tiny);
		font-size: 0.75rem;
		line-height: 1.5;
		cursor: default;
	}

	.tooltip-tags-row {
		display: flex;
		justify-content: center;
		align-items: center;
		flex-wrap: wrap;
		gap: 6px;
		padding-top: 2px;
	}

	.tooltip-tag-pill {
		display: inline-block;
		padding: 2px 10px;
		font-family: var(--pixel);
		font-weight: 800;
		font-size: 0.75rem;
		line-height: 1.3;
		clip-path: var(--notch-clip);
		border: 1px solid rgba(0, 0, 0, 0.25);
		box-shadow: 0 3px 0 var(--pill-shadow);
		text-shadow: 1px 1px 0 rgba(0, 0, 0, 0.5);
	}
</style>
