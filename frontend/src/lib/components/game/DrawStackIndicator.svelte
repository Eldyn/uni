<script lang="ts">
	import { onDestroy } from "svelte";
	import { gsap } from "gsap";
	import { storeGame } from "$stores/game.svelte";
	import { storeAudio } from "$stores/audio.svelte";
	import { storeAnimation } from "$stores/animation.svelte";
	import { debtIntensity } from "./animation/debtIntensity";

	// Number of cards currently stacked on the draw chain (+2 / +4 ruleset).
	let pending = $derived(storeGame.state?.pending_draws ?? 0);
	let tier = $derived(debtIntensity(pending));

	const POP_START_SCALE = 1.35;
	const POP_DURATION_S = 0.45;

	let previousPending = $state(0);
	let badgeElement = $state<HTMLDivElement | null>(null);
	let popTween: gsap.core.Tween | null = null;

	function popBadge(element: HTMLDivElement): void {
		popTween?.kill();
		popTween = gsap.fromTo(
			element,
			{ scale: POP_START_SCALE },
			{
				scale: 1,
				duration: POP_DURATION_S / storeAnimation.speedMultiplier,
				ease: "back.out(2)"
			}
		);
	}

	$effect(() => {
		if (pending > previousPending) {
			// PLACEHOLDER-SFX: sfx.draw-stack.increase, escalating "stack grows"
			// hit each time another +2/+4 is chained; a human may want to scale
			// pitch with the new pending count.
			storeAudio.playSfx("sfx.draw-stack.increase");
			if (storeAnimation.enabled && badgeElement) popBadge(badgeElement);
		}
		previousPending = pending;
	});

	onDestroy(() => popTween?.kill());
</script>

{#if pending > 0}
	<div class="draw-stack-overlay">
		<div class="draw-stack-badge tier-{tier}" bind:this={badgeElement}>+{pending}</div>
	</div>
{/if}

<style>
	.draw-stack-overlay {
		position: fixed;
		top: 50%;
		left: 50%;
		transform: translate(-50%, -50%);
		z-index: 9000;
		pointer-events: none;
		display: flex;
		flex-direction: column;
		align-items: center;
		gap: 0.2em;
	}

	.draw-stack-badge {
		font-family: "FatPixel", sans-serif;
		font-size: 10rem;
		line-height: 1;
		color: #ffd23f;
		text-shadow:
			0 0 10px rgba(255, 100, 0, 0.9),
			4px 4px 0 #b02a00,
			-2px -2px 0 #b02a00;
		transition:
			color 0.25s ease,
			text-shadow 0.25s ease;
	}

	.draw-stack-badge.tier-2 {
		color: #ff9a2e;
		text-shadow:
			0 0 16px rgba(255, 60, 0, 1),
			4px 4px 0 #8f1d00,
			-2px -2px 0 #8f1d00;
	}

	.draw-stack-badge.tier-3 {
		color: #ff4a3a;
		text-shadow:
			0 0 26px rgba(255, 0, 0, 1),
			4px 4px 0 #5e0a00,
			-2px -2px 0 #5e0a00;
	}
</style>
