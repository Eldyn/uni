<script lang="ts">
	import { type Snippet } from "svelte";

	interface Props {
		children: Snippet; // The trigger element (e.g., the item icon)
		tooltipContent: Snippet; // The floating item lore / text
	}

	let { children, tooltipContent }: Props = $props();

	let isVisible = $state(false);
	let tooltipEl: HTMLElement | undefined = $state();

	const EDGE_MARGIN = 8;
	const CURSOR_OFFSET = 15;

	let left = $state(0);
	let top = $state(0);

	// INFO: clip-path on ancestor containers (e.g. .pixel-corners) clips position:fixed
	// descendants. Moving the floating div to the document body escapes any clipped subtree
	// while Svelte's scoped class keeps the style rules intact.
	function portal(node: HTMLElement) {
		document.body.appendChild(node);
		return { destroy: () => node.remove() };
	}

	// Re-measures the tooltip's real, current rendered size on every move
	// instead of once at mount — a one-shot measurement (the previous
	// approach) locks in whatever size happened to exist at that instant,
	// which is wrong for the very first frame content actually renders in.
	// The final position is then clamped into the viewport directly rather
	// than a binary left/right-of-cursor flip, which still overflowed the
	// far edge whenever the tooltip was wider than the remaining space on
	// BOTH sides of the cursor (e.g. long descriptions on a narrow viewport).
	function handleMouseMove(e: MouseEvent) {
		const width = tooltipEl?.offsetWidth ?? 0;
		const height = tooltipEl?.offsetHeight ?? 0;

		const maxLeft = window.innerWidth - width - EDGE_MARGIN;
		const maxTop = window.innerHeight - height - EDGE_MARGIN;

		left = Math.min(Math.max(EDGE_MARGIN, e.clientX + CURSOR_OFFSET), Math.max(EDGE_MARGIN, maxLeft));
		top = Math.min(Math.max(EDGE_MARGIN, e.clientY + CURSOR_OFFSET), Math.max(EDGE_MARGIN, maxTop));
	}
</script>

<div
	class="tooltip-container"
	role="tooltip"
	onmouseenter={(e) => {
		isVisible = true;
		left = e.clientX + CURSOR_OFFSET;
		top = e.clientY + CURSOR_OFFSET;
	}}
	onmouseleave={() => (isVisible = false)}
	onmousemove={handleMouseMove}
>
	{@render children()}

	{#if isVisible}
		<div
			bind:this={tooltipEl}
			class="minecraft-floating-tooltip"
			use:portal
			style:left="{left}px"
			style:top="{top}px"
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
		pointer-events: none;
		z-index: 10001;

		background: rgba(16, 1, 16, 0.94);
		border: 2px solid #2e0664;
		outline: 2px solid #100110;
		padding: 10px 12px;
		border-radius: 4px;
		box-shadow: 0 4px 10px rgba(0, 0, 0, 0.5);
		font-family: var(--mono);
		/* Wraps instead of stretching indefinitely — a long (nowrap) rule
		   description could make the tooltip wider than the viewport itself,
		   which no amount of position-clamping alone can keep on-screen. */
		white-space: normal;
		max-width: min(320px, calc(100vw - 2 * 8px));
	}
</style>
