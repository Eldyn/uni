<script lang="ts">
	import type { Snippet } from "svelte";
	import type { GlossaryTag } from "$lib/glossary/glossary";

	interface Props {
		title?: string;
		tags?: GlossaryTag[];
		children?: Snippet;
		class?: string;
		onclose?: () => void;
	}

	let {
		title,
		tags = [],
		children,
		class: extraClass = "",
		onclose
	}: Props = $props();

	function handleCardClick(e: MouseEvent) {
		const target = e.target as HTMLElement | null;
		if (target?.closest("button, a, input, textarea")) return;
		onclose?.();
	}
</script>

<!-- svelte-ignore a11y_click_events_have_key_events -->
<!-- svelte-ignore a11y_no_static_element_interactions -->
<div
	class="balatro-tooltip-card pixel-bordered {extraClass}"
	onclick={handleCardClick}
>
	{#if title}
		<div class="balatro-title pypx-thick">{title}</div>
	{/if}

	<div class="balatro-content-box pixel-bordered">
		{@render children?.()}
	</div>

	{#if tags.length > 0}
		<div class="balatro-tags-row">
			{#each tags as tag}
				<span
					class="balatro-tag-pill"
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
	.balatro-tooltip-card {
		--pc-fill: #374244;
		--pc-border: #1e2628;
		--pc-width: 2px;
		box-shadow: 0 4px 0 #1b2223;
		padding: 8px 10px;
		display: flex;
		flex-direction: column;
		gap: 6px;
		max-width: 300px;
		width: max-content;
		user-select: none;
		cursor: pointer;
	}

	.balatro-title {
		text-align: center;
		color: #ffffff;
		font-family: var(--pypx);
		font-weight: 800;
		font-size: 0.95rem;
		line-height: 1.1;
		letter-spacing: 0.04em;
		text-shadow: 1px 1px 0 #000000;
	}

	.balatro-content-box {
		--pc-fill: #ffffff;
		--pc-border: #2c3537;
		--pc-width: 2px;
		box-shadow: 0 3px 0 #c2c6ca;
		padding: 8px 10px;
		color: #2c3537;
		font-family: var(--tiny);
		font-size: 0.75rem;
		line-height: 1.35;
		cursor: default;
	}

	.balatro-tags-row {
		display: flex;
		justify-content: center;
		align-items: center;
		flex-wrap: wrap;
		gap: 6px;
		padding-top: 2px;
	}

	.balatro-tag-pill {
		display: inline-block;
		padding: 2px 10px;
		font-family: var(--pypx);
		font-weight: 800;
		font-size: 0.75rem;
		line-height: 1.2;
		border-radius: 4px;
		border: 1px solid rgba(0, 0, 0, 0.25);
		box-shadow: 0 3px 0 var(--pill-shadow);
		text-shadow: 1px 1px 0 rgba(0, 0, 0, 0.5);
	}
</style>
