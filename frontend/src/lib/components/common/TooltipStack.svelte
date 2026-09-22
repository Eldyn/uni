<script lang="ts">
	import { storeTooltipStack, type TooltipStackEntry } from "$stores/tooltipStack.svelte";
	import RichText from "$components/common/RichText.svelte";
	import TooltipCard from "$components/common/TooltipCard.svelte";

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

	// The tree-leave debounce lives on the store so the root Tooltip popover
	// (a separate portal, and this stack's conceptual parent) can cancel it too.
	function handleTreeMouseEnter() {
		storeTooltipStack.treeHoverEnter();
	}

	function handleTreeMouseLeave() {
		storeTooltipStack.treeHoverLeave();
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
				tabindex="-1"
				class="tooltip-stack-item"
				style:left="{item.x}px"
				style:top="{item.y}px"
				style:z-index={20000 + item.depth * 20}
				use:handleMeasure={item}
				onmouseenter={handleTreeMouseEnter}
				onmouseleave={handleTreeMouseLeave}
			>
				<TooltipCard
					title={item.title}
					tags={item.tags}
					linkColor={item.linkColor}
					onclose={() => storeTooltipStack.close(item.id)}
				>
					<RichText text={item.description} allowKeywords={true} tooltipParentId={item.id} />
				</TooltipCard>
			</div>
		{/each}
	</div>
{/if}

<style>
	/* One below the root tooltip popover (19999) so the backdrop never covers
	   it, and well below the stack items (20000+) themselves. */
	.tooltip-stack-backdrop {
		position: fixed;
		inset: 0;
		z-index: 19998;
		pointer-events: auto;
	}

	.tooltip-stack-item {
		position: fixed;
		pointer-events: auto;
	}
</style>
