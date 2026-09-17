<script lang="ts">
	import { type Snippet, tick } from "svelte";
	import {
		computeAnchorPlacement,
		type Side,
		type Align,
		type Rect
	} from "$lib/utils/anchorPlacement";

	interface Props {
		children: Snippet;
		tooltipContent: Snippet;
		side?: Side;
		align?: Align;
		offset?: number;
		openDelay?: number;
		id?: string;
		class?: string;
		avoidRects?: Rect[];
	}

	let {
		children,
		tooltipContent,
		side = "top",
		align = "center",
		offset = 8,
		openDelay = 150,
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
		openTimer = setTimeout(async () => {
			isVisible = true;
			await tick();
			updatePosition();
		}, openDelay);
	}

	function handleMouseLeave() {
		clearTimeout(openTimer);
		isVisible = false;
	}

	async function handleFocusIn() {
		clearTimeout(openTimer);
		isVisible = true;
		await tick();
		updatePosition();
	}

	function handleFocusOut() {
		clearTimeout(openTimer);
		isVisible = false;
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
			class="pixel-bordered pixel-popover-tooltip"
			use:portal
			data-side={placedSide}
			style:left="{posX}px"
			style:top="{posY}px"
		>
			{@render tooltipContent()}
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

		padding: 8px 12px;
		font-family: var(--tiny);
		font-size: 0.75rem;
		line-height: 1.3;
		color: var(--text-h);
		text-shadow: 1px 1px 0 var(--pixel-shadow, #000);

		--pc-fill: #100110;
		--pc-border: #2e0664;
		box-shadow: var(--elevation-2);

		white-space: normal;
		max-width: 320px;
	}
</style>
