<script lang="ts">
	import { onDestroy } from "svelte";
	import { gsap } from "gsap";
	import { storeGame } from "$stores/game.svelte";
	import { storeAudio } from "$stores/audio.svelte";
	import { storeAnimation } from "$stores/animation.svelte";
	import { storeWebglCapability } from "$stores/webglCapability.svelte";
	import { storeI18n } from "$stores/i18n.svelte";
	import * as m from "$lib/paraglide/messages.js";
	import { debtIntensity } from "./animation/debtIntensity";

	// Number of cards currently stacked on the draw chain (+2 / +4 ruleset).
	let pending = $derived(storeGame.state?.pending_draws ?? 0);
	let tier = $derived(debtIntensity(pending));

	const POP_START_SCALE = 1.35;
	const POP_DURATION_S = 0.45;

	let previousPending = $state(0);
	let badgeElement = $state<HTMLDivElement | null>(null);
	let popTween: gsap.core.Tween | null = null;

	let targetUsername = $derived(storeGame.debtVictimUsername);
	let isLocalVictim = $derived(storeGame.isDebtVictim);
	let targetLabel = $derived.by(() => {
		if (!targetUsername) return "";
		if (isLocalVictim) {
			return m.game_debt_target_you({}, { locale: storeI18n.locale });
		}
		return targetUsername;
	});

	let timer = $derived(storeGame.activeTimer);
	let reducedMotion = $derived(storeWebglCapability.reducedMotion);
	let isUrgent = $derived(
		timer !== null &&
			timer.durationMs > 0 &&
			(timer.deadlineAt - Date.now()) / timer.durationMs <= 0.25
	);

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
			// INFO: GSAP does not tick in a hidden tab, so a pop queued there
			// would play stale on return; the badge still shows the new total.
			if (storeAnimation.enabled && badgeElement && !document.hidden) popBadge(badgeElement);
		}
		previousPending = pending;
	});

	onDestroy(() => popTween?.kill());
</script>

{#if pending > 0}
	<div class="draw-stack-overlay" data-testid="draw-stack-overlay">
		<div
			class="draw-stack-badge tier-{tier}"
			class:urgent={isUrgent}
			class:motion-reduced={reducedMotion}
			bind:this={badgeElement}
			data-testid="draw-stack-badge"
		>
			+{pending}
		</div>
		{#if targetLabel}
			<div
				class="draw-stack-target-pill tier-{tier}"
				class:is-local={isLocalVictim}
				class:urgent={isUrgent}
				data-testid="draw-stack-target-pill"
			>
				<span class="target-arrow" aria-hidden="true">▶</span>
				<span class="target-name">{targetLabel}</span>
			</div>
		{/if}
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
		gap: 0.35em;
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
		animation: badge-breathe 2.2s ease-in-out infinite alternate;
	}

	.draw-stack-badge.urgent {
		animation: badge-panic 0.35s ease-in-out infinite alternate;
	}

	.draw-stack-badge.motion-reduced {
		animation: none;
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

	.draw-stack-target-pill {
		display: inline-flex;
		align-items: center;
		gap: 8px;
		padding: 6px 16px;
		background: rgba(14, 12, 12, 0.9);
		border: 2px solid #ffd23f;
		box-shadow:
			0 4px 16px rgba(0, 0, 0, 0.7),
			0 0 12px rgba(255, 210, 63, 0.3);
		color: #ffd23f;
		font-family: monospace;
		font-size: 1.1rem;
		font-weight: bold;
		letter-spacing: 0.08em;
		text-transform: uppercase;
		pointer-events: none;
		border-radius: 4px;
	}

	.draw-stack-target-pill.tier-2 {
		border-color: #ff9a2e;
		color: #ff9a2e;
		box-shadow:
			0 4px 16px rgba(0, 0, 0, 0.7),
			0 0 16px rgba(255, 154, 46, 0.4);
	}

	.draw-stack-target-pill.tier-3 {
		border-color: #ff4a3a;
		color: #ff4a3a;
		box-shadow:
			0 4px 16px rgba(0, 0, 0, 0.7),
			0 0 20px rgba(255, 74, 58, 0.5);
	}

	.draw-stack-target-pill.is-local {
		background: rgba(140, 20, 10, 0.94);
		border-color: #ff4a3a;
		color: #ffffff;
		box-shadow:
			0 4px 16px rgba(0, 0, 0, 0.7),
			0 0 24px rgba(255, 74, 58, 0.75);
	}

	.target-arrow {
		font-size: 0.8em;
	}

	@keyframes badge-breathe {
		0% {
			transform: scale(1);
		}
		100% {
			transform: scale(1.06);
		}
	}

	@keyframes badge-panic {
		0% {
			transform: scale(1.02);
		}
		100% {
			transform: scale(1.12);
		}
	}
</style>
