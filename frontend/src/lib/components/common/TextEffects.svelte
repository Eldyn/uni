<script lang="ts">
	type Effect = "undulate" | "shine" | "shake" | "none";

	let {
		text,
		effect = "none" as Effect,
		class: className = "",
		// Font family for the rendered text (e.g. "FatPixel" or "var(--heading)").
		// Needed because the chars are <span>s, so they don't inherit element
		// rules like `h1 { font-family: ... }`.
		font = "",
		// Text color, applied to every branch (shine has its own
		// shineColor/shineBaseColor sweep instead, this is the plain-text color).
		color = "",
		// undulate
		frequency = 0.12, // seconds of phase offset per character
		amplitude = 10, // px vertical displacement
		speed = 1.2, // seconds per full cycle
		// shine
		shineSpeed = 2.5, // seconds per sweep
		shineColor = "rgba(255, 255, 255, 0.9)",
		shineBaseColor = "var(--text)", // base text color (currentColor breaks with background-clip: text)
		// shake
		shakeIntensity = 3, // max px displacement
		shakeSpeed = 0.4 // seconds per shake cycle
	}: {
		text: string;
		effect?: Effect;
		class?: string;
		font?: string;
		color?: string;
		frequency?: number;
		amplitude?: number;
		speed?: number;
		shineSpeed?: number;
		shineColor?: string;
		shineBaseColor?: string;
		shakeIntensity?: number;
		shakeSpeed?: number;
	} = $props();

	const chars = $derived(text.split(""));
	const fontStyle = $derived(font ? `font-family: ${font};` : "");
	// Not folded into fontStyle: shine already colors itself via
	// --shine-base/--shine-color (background-clip: text), so a plain inline
	// `color` there would win over the class's `color: transparent` and break
	// the sweep effect. Every other branch applies it directly.
	const colorStyle = $derived(color ? `color: ${color};` : "");
</script>

{#if effect === "shine"}
	<span
		class="shine {className}"
		style="{fontStyle} --shine-speed: {shineSpeed}s; --shine-color: {shineColor}; --shine-base: {shineBaseColor}"
		>{text}</span
	>
{:else if effect === "undulate"}
	<span
		class="char-wrap {className}"
		style="{fontStyle}{colorStyle} --amp: {amplitude}px; --speed: {speed}s; --freq: {frequency}s"
	>
		{#each chars as char, i}
			<span class="char undulate-char" style="--i: {i}" aria-hidden={i > 0 ? "true" : undefined}
				>{char === " " ? " " : char}</span
			>
		{/each}
		<span class="sr-only">{text}</span>
	</span>
{:else if effect === "shake"}
	<span
		class="char-wrap {className}"
		style="{fontStyle}{colorStyle} --amp: {shakeIntensity}px; --speed: {shakeSpeed}s"
	>
		{#each chars as char, i}
			<span class="char shake-char" style="--i: {i}" aria-hidden={i > 0 ? "true" : undefined}
				>{char === " " ? " " : char}</span
			>
		{/each}
		<span class="sr-only">{text}</span>
	</span>
{:else}
	<span class={className} style="{fontStyle}{colorStyle}">{text}</span>
{/if}

<style>
	.char-wrap {
		display: inline-flex;
		align-items: center;
		/* Reserve real layout room equal to the animation travel so a translated
		   char is never clipped by the wrapper or an ancestor that sizes/clips to
		   this box (e.g. a flex button). */
		padding: var(--amp, 10px);
		overflow: visible;
	}

	.char {
		display: inline-block;
		will-change: transform;
	}

	/* ── Undulate ─────────────────────────────────────────── */
	.undulate-char {
		animation: undulate var(--speed, 1.2s) ease-in-out infinite;
		/* Negative delay = start each char at a different point in the cycle */
		animation-delay: calc(var(--i) * var(--freq, 0.12s) * -1);
	}

	@keyframes undulate {
		0%,
		100% {
			transform: translateY(0);
		}
		50% {
			transform: translateY(calc(var(--amp, 10px) * -1));
		}
	}

	/* ── Shine ────────────────────────────────────────────── */
	/* Hard stops = crisp pixel edge; steps(30) = discrete jumps not smooth interpolation. */
	.shine {
		display: inline-block;
		background: linear-gradient(
			110deg,
			var(--shine-base, var(--text)) 45.5%,
			var(--shine-color, rgba(255, 255, 255, 0.9)) 45.5%,
			var(--shine-color, rgba(255, 255, 255, 0.9)) 54.5%,
			var(--shine-base, var(--text)) 54.5%
		);
		background-size: 300% auto;
		-webkit-background-clip: text;
		background-clip: text;
		color: transparent;
		animation: shine-sweep var(--shine-speed, 2.5s) steps(30, end) infinite;
	}

	@keyframes shine-sweep {
		from {
			background-position: 200% center;
		}
		to {
			background-position: -100% center;
		}
	}

	/* ── Shake ────────────────────────────────────────────── */
	.shake-char {
		animation: shake var(--speed, 0.4s) ease-in-out infinite;
		/* Stagger so chars don't all move identically */
		animation-delay: calc(var(--i) * 0.05s);
	}

	@keyframes shake {
		0% {
			transform: translate(0, 0);
		}
		15% {
			transform: translate(calc(var(--amp, 3px) * -1), calc(var(--amp, 3px) * 0.5));
		}
		30% {
			transform: translate(calc(var(--amp, 3px) * 0.8), calc(var(--amp, 3px) * -0.6));
		}
		45% {
			transform: translate(calc(var(--amp, 3px) * -0.5), calc(var(--amp, 3px) * -0.9));
		}
		60% {
			transform: translate(calc(var(--amp, 3px) * 0.9), calc(var(--amp, 3px) * 0.3));
		}
		75% {
			transform: translate(calc(var(--amp, 3px) * -0.7), calc(var(--amp, 3px) * 0.7));
		}
		90% {
			transform: translate(calc(var(--amp, 3px) * 0.4), calc(var(--amp, 3px) * -0.4));
		}
		100% {
			transform: translate(0, 0);
		}
	}

	/* Accessible label for screen readers (per-char aria-hidden is set above) */
	.sr-only {
		position: absolute;
		width: 1px;
		height: 1px;
		padding: 0;
		margin: -1px;
		overflow: hidden;
		clip: rect(0, 0, 0, 0);
		white-space: nowrap;
		border: 0;
	}
</style>
