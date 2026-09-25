<script lang="ts">
	/**
	 * Blog-style section divider: a 2px rule with an optional uppercase Pypx
	 * label, matching uni-blog's `Divisor.astro`.
	 *
	 * - align="left"   → `AUDIO ────────────`
	 * - align="center" → `────── AUDIO ──────`
	 * - align="right"  → `──────────── AUDIO`
	 */
	let {
		text = "",
		align = "left"
	}: {
		text?: string;
		align?: "left" | "center" | "right";
	} = $props();
</script>

<div class="divisor divisor--{align}" role="separator">
	{#if text}<span class="divisor__text">{text}</span>{/if}
</div>

<style>
	.divisor {
		display: flex;
		align-items: center;
		gap: var(--space-3);
	}
	.divisor::before,
	.divisor::after {
		content: "";
		height: 2px;
		background: var(--border);
		flex: 1 1 auto;
	}
	.divisor--left::before {
		display: none;
	}
	.divisor--right::after {
		display: none;
	}
	.divisor__text {
		font-family: var(--pypx);
		font-weight: 800;
		text-transform: uppercase;
		letter-spacing: 0.06em;
		color: var(--text-h);
		white-space: nowrap;
	}
</style>
