<!-- A faint ring of chevrons around the table showing the play direction. It
     drifts slowly the way play goes; a reverse squash-flips it (scale.x runs
     through zero, so the chevrons turn the other way while the ring stays on
     the table plane) with a brief brighten. Everything steps on the shared
     12fps grid like the felt ripple. Reduced motion keeps the ring but holds
     it still and snaps it to the current direction. -->
<script lang="ts">
	import { T, useTask, useThrelte } from "@threlte/core";
	import { onDestroy, untrack } from "svelte";
	import { gsap } from "gsap";
	import { DoubleSide } from "three";
	import { storeAnimation } from "$stores/animation.svelte";
	import { storeGame } from "$stores/game.svelte";
	import { storeDirectionRing } from "$stores/directionRing.svelte";
	import {
		RING_FLIP_PEAK_OPACITY,
		RING_FLIP_SECONDS,
		RING_IDLE_OPACITY,
		RING_SPIN_RADIANS_PER_SECOND,
		type DirectionSign
	} from "../animation/directionRing";
	import { AMBIENT_STEP_FPS, AMBIENT_STEP_MS, stepIndexAt } from "./ripple/ripplePlan";
	import { directionRingTexture } from "./directionRingTexture";
	import type { MatPlacement } from "../layout/playmat";

	let { mat }: { mat: MatPlacement } = $props();

	const RING_Y = -0.012;
	const RING_FIT = 0.94;
	const RING_TINT = "#ffffff";

	storeDirectionRing.set(storeGame.state?.play_direction ?? 1);

	const texture = directionRingTexture();
	const { invalidate } = useThrelte();

	let ringSize = $derived(Math.min(mat.size[0], mat.size[1]) * RING_FIT);

	// Rendered pose, all written on 12fps steps only.
	let mirror = $state<number>(storeDirectionRing.sign);
	let opacity = $state(RING_IDLE_OPACITY);
	let spin = $state(0);

	const flipState = { mirror: storeDirectionRing.sign as number, glow: 0 };
	const pendingFlips: DirectionSign[] = [];
	let activeTween: gsap.core.Tween | null = null;
	let lastCueToken = storeDirectionRing.token;
	let lastSpinStep = -1;

	function stepped(progress: number, steps: number): number {
		return Math.floor(progress * steps) / steps;
	}

	function renderFlipPose(): void {
		mirror = flipState.mirror;
		opacity = RING_IDLE_OPACITY + (RING_FLIP_PEAK_OPACITY - RING_IDLE_OPACITY) * flipState.glow;
		invalidate();
	}

	function settle(sign: DirectionSign): void {
		activeTween?.kill();
		activeTween = null;
		pendingFlips.length = 0;
		flipState.mirror = sign;
		flipState.glow = 0;
		renderFlipPose();
	}

	function runNextFlip(): void {
		if (activeTween) return;
		const target = pendingFlips.shift();
		if (target === undefined) return;

		const duration = RING_FLIP_SECONDS / Math.max(0.1, storeAnimation.speedMultiplier);
		const steps = Math.max(1, Math.round(duration * AMBIENT_STEP_FPS));
		const from = flipState.mirror;
		const flip = { progress: 0 };
		activeTween = gsap.to(flip, {
			progress: 1,
			duration,
			ease: "power2.inOut",
			onUpdate: () => {
				const progress = stepped(flip.progress, steps);
				flipState.mirror = from + (target - from) * progress;
				flipState.glow = Math.sin(Math.PI * progress);
				renderFlipPose();
			},
			onComplete: () => {
				activeTween = null;
				flipState.mirror = target;
				flipState.glow = 0;
				renderFlipPose();
				runNextFlip();
			}
		});
	}

	$effect(() => {
		const token = storeDirectionRing.token;
		if (token === lastCueToken) return;
		lastCueToken = token;
		const flips = storeDirectionRing.takeFlips();
		if (flips.length === 0) return;
		untrack(() => {
			if (!storeAnimation.enabled) {
				settle(flips[flips.length - 1]);
				return;
			}
			pendingFlips.push(...flips);
			runNextFlip();
		});
	});

	useTask(
		() => {
			if (!storeAnimation.enabled) return;
			const step = stepIndexAt(performance.now());
			if (step === lastSpinStep) return;
			const elapsedSteps = lastSpinStep < 0 ? 0 : step - lastSpinStep;
			lastSpinStep = step;
			const spinDelta = RING_SPIN_RADIANS_PER_SECOND * (AMBIENT_STEP_MS / 1000) * elapsedSteps;
			spin -= storeDirectionRing.sign * spinDelta;
			invalidate();
		},
		{ autoInvalidate: false }
	);

	onDestroy(() => {
		activeTween?.kill();
		activeTween = null;
	});
</script>

{#if texture}
	<T.Mesh
		position.y={RING_Y}
		rotation={[-Math.PI / 2, 0, spin]}
		scale.x={mirror}
		data-testid="direction-ring"
	>
		<T.PlaneGeometry args={[ringSize, ringSize]} />
		<T.MeshBasicMaterial
			map={texture}
			color={RING_TINT}
			transparent
			{opacity}
			side={DoubleSide}
			depthWrite={false}
			toneMapped={false}
		/>
	</T.Mesh>
{/if}
