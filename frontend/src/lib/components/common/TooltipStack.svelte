<script lang="ts">
	import { storeTooltipStack, type TooltipStackEntry } from "$stores/tooltipStack.svelte";
	import RichText from "$components/common/RichText.svelte";
	import { storeI18n } from "$stores/i18n.svelte";
	import * as m from "$lib/paraglide/messages.js";

	function portal(node: HTMLElement) {
		document.body.appendChild(node);
		return {
			destroy: () => {
				if (node.parentNode) node.remove();
			}
		};
	}

	function handleMeasure(node: HTMLElement, item: TooltipStackEntry) {
		storeTooltipStack.measureAndReposition(item.id, {
			width: node.offsetWidth,
			height: node.offsetHeight
		});
	}

	function handleBackdropClick(e: MouseEvent) {
		// Only close if click was directly on backdrop
		if (e.target === e.currentTarget) {
			storeTooltipStack.closeAll();
		}
	}
</script>

{#if storeTooltipStack.stack.length > 0}
	<!-- svelte-ignore a11y_click_events_have_key_events -->
	<!-- svelte-ignore a11y_no_static_element_interactions -->
	<div class="tooltip-stack-backdrop" use:portal onclick={handleBackdropClick}>
		{#each storeTooltipStack.stack as item (item.id)}
			<div
				id={item.id}
				role="dialog"
				aria-modal="false"
				aria-label={item.title}
				class="pixel-bordered pixel-popover-glossary"
				style:left="{item.x}px"
				style:top="{item.y}px"
				style:z-index={20000 + item.depth * 20}
				use:handleMeasure={item}
			>
				<div class="glossary-header">
					<span class="glossary-title">{item.title}</span>
					<button
						type="button"
						class="glossary-close-btn"
						aria-label={m.glossary_close({}, { locale: storeI18n.locale })}
						onclick={() => storeTooltipStack.close(item.id)}
					>
						✕
					</button>
				</div>
				<div class="glossary-body">
					<RichText
						text={item.description}
						allowKeywords={true}
						tooltipParentId={item.id}
					/>
				</div>
			</div>
		{/each}
	</div>
{/if}

<style>
	.tooltip-stack-backdrop {
		position: fixed;
		inset: 0;
		z-index: 19999;
		pointer-events: auto;
	}

	.pixel-popover-glossary {
		position: fixed;
		pointer-events: auto;
		width: 280px;
		max-width: calc(100vw - 24px);
		padding: 8px 10px;
		font-family: var(--tiny);
		font-size: 0.8rem;
		line-height: 1.35;
		color: var(--text-h);
		text-shadow: 1px 1px 0 var(--pixel-shadow, #000);

		--pc-fill: #100110;
		--pc-border: #431474;
		box-shadow: var(--elevation-3, 0 10px 25px -5px rgba(0, 0, 0, 0.5));
	}

	.glossary-header {
		display: flex;
		align-items: center;
		justify-content: space-between;
		margin-bottom: 6px;
		padding-bottom: 4px;
		border-bottom: 1px dashed rgba(255, 255, 255, 0.2);
	}

	.glossary-title {
		font-weight: 800;
		color: var(--brand, #38bdf8);
		text-transform: uppercase;
		letter-spacing: 0.05em;
		font-size: 0.75rem;
	}

	.glossary-close-btn {
		background: none;
		border: none;
		color: var(--text-muted, #94a3b8);
		cursor: pointer;
		font-size: 0.75rem;
		padding: 2px 4px;
		line-height: 1;
		transition: color 0.15s ease;
	}

	.glossary-close-btn:hover {
		color: var(--danger, #ef4444);
	}

	.glossary-body {
		color: var(--text);
		font-size: 0.8rem;
		word-break: break-word;
	}
</style>
