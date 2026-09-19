<script lang="ts">
	import { type Snippet, tick, untrack } from "svelte";
	import {
		computeAnchorPlacement,
		type Side,
		type Align,
		type Rect
	} from "$lib/utils/anchorPlacement";
	import type { GlossaryTag } from "$lib/glossary/glossary";
	import TooltipCard from "$components/common/TooltipCard.svelte";
	import { storeTooltipStack } from "$stores/tooltipStack.svelte";

	interface Props {
		children: Snippet;
		tooltipContent: Snippet;
		title?: string;
		tags?: GlossaryTag[];
		side?: Side;
		align?: Align;
		offset?: number;
		openDelay?: number;
		closeDelay?: number;
		interactive?: boolean;
		id?: string;
		class?: string;
		avoidRects?: Rect[];
	}

	let {
		children,
		tooltipContent,
		title,
		tags,
		side = "top",
		align = "center",
		offset = 8,
		openDelay = 150,
		closeDelay = 300,
		interactive = false,
		id,
		class: extraClass = "",
		avoidRects
	}: Props = $props();

	let isVisible = $state(false);
	let containerEl: HTMLElement | undefined = $state();
	let tooltipEl: HTMLElement | undefined = $state();

	const fallbackId = `tooltip-${Math.random().toString(36).slice(2, 9)}`;
	const tooltipId = $derived(id ?? fallbackId);

	let posX = $state(0);
	let posY = $state(0);
	let placedSide = $state<Side>("top");

	let openTimer: ReturnType<typeof setTimeout> | undefined;
	let closeTimer: ReturnType<typeof setTimeout> | undefined;

	// Whether the pointer is currently over this tooltip's own trigger or
	// popover (NOT any child glossary tooltip, which is portaled separately
	// by TooltipStack and can't be tracked via DOM containment).
	let isHoveringTree = $state(false);

	/**
	 * Starts the close timer, unless a child glossary tooltip opened from
	 * inside this popover (storeTooltipStack) is still open. Moving the
	 * pointer off this popover's DOM and onto a child tooltip's DOM (a
	 * separate portal elsewhere in the document) fires this tooltip's own
	 * mouseleave even though the user never really left the tooltip tree, so
	 * the close must defer to whichever closes last, the child stack.
	 */
	function scheduleClose() {
		if (storeTooltipStack.stack.length > 0) return;
		closeTimer = setTimeout(() => {
			isVisible = false;
		}, closeDelay);
	}

	// Once every child glossary tooltip has closed, resume this tooltip's own
	// leave-driven close if the pointer had already left it while children
	// were still open (scheduleClose() no-op'd above, so nothing was pending).
	// Only storeTooltipStack.stack.length should re-run this effect — reads of
	// interactive/isVisible/isHoveringTree are wrapped in untrack() so their
	// own, unrelated changes (e.g. re-entering the tooltip) don't re-fire it
	// and race a second, untracked setTimeout against scheduleClose()'s.
	$effect(() => {
		const hasOpenChildren = storeTooltipStack.stack.length > 0;
		untrack(() => {
			if (hasOpenChildren) {
				clearTimeout(closeTimer);
				return;
			}
			if (!interactive || !isVisible || isHoveringTree) return;
			closeTimer = setTimeout(() => {
				isVisible = false;
			}, closeDelay);
		});
	});

	function portal(node: HTMLElement) {
		document.body.appendChild(node);
		return {
			destroy: () => {
				if (node.parentNode) node.remove();
			}
		};
	}

	function updatePosition() {
		if (!containerEl || !tooltipEl) return;

		const triggerRect = containerEl.getBoundingClientRect();
		const contentSize = {
			width: tooltipEl.offsetWidth,
			height: tooltipEl.offsetHeight
		};

		const placement = computeAnchorPlacement(triggerRect, contentSize, {
			side,
			align,
			offset,
			avoidRects
		});

		posX = placement.x;
		posY = placement.y;
		placedSide = placement.side;
	}

	function handleMouseEnter() {
		isHoveringTree = true;
		storeTooltipStack.treeHoverEnter();
		clearTimeout(openTimer);
		clearTimeout(closeTimer);
		openTimer = setTimeout(async () => {
			isVisible = true;
			await tick();
			updatePosition();
		}, openDelay);
	}

	function handleMouseLeave() {
		isHoveringTree = false;
		storeTooltipStack.treeHoverLeave();
		clearTimeout(openTimer);
		if (interactive) {
			scheduleClose();
		} else {
			isVisible = false;
		}
	}

	function handleTooltipMouseEnter() {
		isHoveringTree = true;
		storeTooltipStack.treeHoverEnter();
		if (!interactive) return;
		clearTimeout(closeTimer);
	}

	function handleTooltipMouseLeave() {
		isHoveringTree = false;
		storeTooltipStack.treeHoverLeave();
		if (!interactive) return;
		clearTimeout(closeTimer);
		scheduleClose();
	}

	async function handleFocusIn() {
		clearTimeout(openTimer);
		clearTimeout(closeTimer);
		isVisible = true;
		await tick();
		updatePosition();
	}

	function handleFocusOut(e: FocusEvent) {
		clearTimeout(openTimer);
		if (interactive) {
			const related = e.relatedTarget as Node | null;
			if (related && (containerEl?.contains(related) || tooltipEl?.contains(related))) {
				return;
			}
			closeTimer = setTimeout(() => {
				isVisible = false;
			}, closeDelay);
		} else {
			isVisible = false;
		}
	}

	$effect(() => {
		if (!isVisible) return;

		const update = () => updatePosition();
		window.addEventListener("scroll", update, { passive: true, capture: true });
		window.addEventListener("resize", update, { passive: true });

		return () => {
			window.removeEventListener("scroll", update, true);
			window.removeEventListener("resize", update);
		};
	});

	$effect(() => {
		if (!containerEl) return;
		const target =
			containerEl.querySelector<HTMLElement>("button, input, select, textarea, [tabindex]") ??
			containerEl;

		if (isVisible) {
			target.setAttribute("aria-describedby", tooltipId);
		} else {
			target.removeAttribute("aria-describedby");
		}
	});
</script>

<!-- svelte-ignore a11y_no_static_element_interactions -->
<div
	bind:this={containerEl}
	class="tooltip-container {extraClass}"
	onmouseenter={handleMouseEnter}
	onmouseleave={handleMouseLeave}
	onfocusin={handleFocusIn}
	onfocusout={handleFocusOut}
>
	{@render children()}

	{#if isVisible}
		<div
			bind:this={tooltipEl}
			id={tooltipId}
			role="tooltip"
			class="pixel-popover-tooltip"
			class:interactive
			use:portal
			data-side={placedSide}
			style:left="{posX}px"
			style:top="{posY}px"
			onmouseenter={handleTooltipMouseEnter}
			onmouseleave={handleTooltipMouseLeave}
			onfocusin={() => clearTimeout(closeTimer)}
			onfocusout={handleFocusOut}
		>
			<TooltipCard {title} tags={tags ?? []} onclose={() => (isVisible = false)}>
				{@render tooltipContent()}
			</TooltipCard>
		</div>
	{/if}
</div>

<style>
	.tooltip-container {
		display: inline-block;
		width: 100%;
	}

	/* Above the tooltip-stack backdrop (19998) but below its stack items
	   (20000+): the backdrop must never sit between this popover and the
	   pointer, or moving into a child glossary tooltip would read as leaving
	   the root and close it. */
	.pixel-popover-tooltip {
		position: fixed;
		pointer-events: none;
		z-index: 19999;

		white-space: normal;
	}

	.pixel-popover-tooltip.interactive {
		pointer-events: auto;
	}

	/* Invisible hit-area extension so the pointer can travel from the trigger
	   into the popover without the closeTimer firing. This is a plain,
	   absolutely-positioned pseudo-element scoped to `.pixel-popover-tooltip`
	   only — the popover no longer carries `.pixel-bordered` itself (that now
	   lives on TooltipCard's own inner card), so this rule can no longer
	   clobber `.pixel-bordered::before`'s `inset: 0` border layer. */
	.pixel-popover-tooltip.interactive::before {
		content: "";
		position: absolute;
		inset: -10px;
		z-index: -1;
	}
</style>
