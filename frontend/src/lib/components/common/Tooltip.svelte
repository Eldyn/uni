<script lang="ts">
	import { type Snippet } from "svelte";

	interface Props {
		children: Snippet; // The trigger element (e.g., the item icon)
		tooltipContent: Snippet; // The floating item lore / text
	}

	let { children, tooltipContent }: Props = $props();

	let isVisible = $state(false);
	let mouseX = $state(0);
	let mouseY = $state(0);

	let shiftX = $state(0); // 0 = right of cursor, 1 = left of cursor
	let shiftY = $state(0); // 0 = below cursor, 1 = above cursor

	let tooltipWidth = $state(0);
	let tooltipHeight = $state(0);

	// INFO: clip-path on ancestor containers (e.g. .pixel-corners) clips position:fixed
	// descendants. Moving the floating div to the document body escapes any clipped subtree
	// while Svelte's scoped class keeps the style rules intact.
	// Measurement happens after appendChild so offsetWidth/offsetHeight reflect real layout.
	function portal(node: HTMLElement) {
		document.body.appendChild(node);
		tooltipWidth = node.offsetWidth;
		tooltipHeight = node.offsetHeight;
		return { destroy: () => node.remove() };
	}

	function handleMouseMove(e: MouseEvent) {
		mouseX = e.clientX;
		mouseY = e.clientY;

		shiftX = mouseX + tooltipWidth > window.innerWidth ? 1 : 0;
		shiftY = mouseY + tooltipHeight > window.innerHeight ? 1 : 0;
	}
</script>

<div
	class="tooltip-container"
	role="tooltip"
	onmouseenter={() => (isVisible = true)}
	onmouseleave={() => (isVisible = false)}
	onmousemove={handleMouseMove}
>
	{@render children()}

	{#if isVisible}
		<div
			class="minecraft-floating-tooltip"
			use:portal
			style:--mouse-x="{mouseX}px"
			style:--mouse-y="{mouseY}px"
			style:--shift-x={shiftX ? "-100%" : "15px"}
			style:--shift-y={shiftY ? "-100%" : "15px"}
		>
			{@render tooltipContent()}
		</div>
	{/if}
</div>

<style>
	.tooltip-container {
		display: inline-block;
		cursor: pointer;
	}

	.minecraft-floating-tooltip {
		position: fixed;
		top: var(--mouse-y);
		left: var(--mouse-x);

		transform: translate(var(--shift-x), var(--shift-y));

		pointer-events: none;
		z-index: 9999;

		background: rgba(16, 1, 16, 0.94);
		border: 2px solid #2e0664;
		outline: 2px solid #100110;
		padding: 10px 12px;
		border-radius: 4px;
		box-shadow: 0 4px 10px rgba(0, 0, 0, 0.5);
		font-family: "JetBrainsMono", monospace;
		white-space: nowrap;
	}
</style>
