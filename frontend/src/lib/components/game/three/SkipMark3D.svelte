<!-- A camera-facing X mark stamped over a seat when that player loses their
     turn to a skip. Scale springs in (GSAP `back.out`) and holds until the
     store ends the presentation; a zero scale keeps it invisible when idle, so
     the sprite is always mounted (no first-activation ref race). -->
<script lang="ts">
	import { onDestroy } from "svelte";
	import { T } from "@threlte/core";
	import { gsap } from "gsap";
	import { storeAnimation } from "$stores/animation.svelte";
	import { skipMarkTexture } from "./skipMarkTexture";
	import { RENDER_ORDER } from "./renderOrder";

	let {
		position,
		size,
		active,
		token,
		renderOrder = RENDER_ORDER.seatSprite + 1
	}: {
		position: [number, number, number];
		/** World-space edge of the fully sprung mark. */
		size: number;
		active: boolean;
		/** Bumps to retrigger the spring when a seat is skipped repeatedly. */
		token: number;
		renderOrder?: number;
	} = $props();

	const texture = skipMarkTexture();
	let progress = $state(0);
	let tween: gsap.core.Tween | null = null;

	$effect(() => {
		// INFO: `token` is the retrigger key — read it so a repeat skip for the
		//       same seat replays the spring instead of sitting at full scale.
		void token;
		tween?.kill();
		tween = null;

		if (!active || !storeAnimation.enabled || !texture) {
			progress = 0;
			return;
		}

		const duration = 0.42 / Math.max(0.1, storeAnimation.speedMultiplier);
		const counter = { value: 0 };
		tween = gsap.to(counter, {
			value: 1,
			duration,
			ease: "back.out(2.2)",
			onUpdate: () => (progress = counter.value)
		});

		return () => {
			tween?.kill();
			tween = null;
		};
	});

	onDestroy(() => {
		tween?.kill();
		tween = null;
	});
</script>

{#if texture}
	<T.Sprite {position} {renderOrder} scale={size * progress}>
		<T.SpriteMaterial map={texture} transparent depthWrite={false} toneMapped={false} />
	</T.Sprite>
{/if}
