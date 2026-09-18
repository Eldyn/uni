<script lang="ts">
	import { type Snippet, tick } from "svelte";
	import {
		computeAnchorPlacement,
		type Side,
		type Align,
		type Rect
	} from "$lib/utils/anchorPlacement";
	import type { GlossaryTag } from "$lib/glossary/glossary";
	import TooltipCard from "$components/common/TooltipCard.svelte";

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
		clearTimeout(openTimer);
		clearTimeout(closeTimer);
		openTimer = setTimeout(async () => {
			isVisible = true;
			await tick();
			updatePosition();
		}, openDelay);
	}

	function handleMouseLeave() {
		clearTimeout(openTimer);
		if (interactive) {
			closeTimer = setTimeout(() => {
				isVisible = false;
			}, closeDelay);
		} else {
			isVisible = false;
		}
	}

	function handleTooltipMouseEnter() {
		if (!interactive) return;
		clearTimeout(closeTimer);
	}

	function handleTooltipMouseLeave() {
		if (!interactive) return;
		clearTimeout(closeTimer);
		closeTimer = setTimeout(() => {
			isVisible = false;
		}, closeDelay);
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

	.pixel-popover-tooltip {
		position: fixed;
		pointer-events: none;
		z-index: 10001;

		white-space: normal;
		max-width: 320px;
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
