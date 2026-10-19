<!-- The 3D avatar stage: a Threlte orthographic canvas showing the baked strip
     on a textured plane, with a ground glow disc under it. The pose is static;
     the strip steps through its 5 frames on the shared 120 ms clock and asks
     for a render on each step, stopping while the tab is hidden. Without WebGL
     the plain Avatar is the fallback, so the preview never blocks the screen. -->
<script lang="ts">
	import { onDestroy, onMount } from "svelte";
	import { Canvas, T } from "@threlte/core";
	import type { CanvasTexture } from "three";
	import Avatar from "$components/common/Avatar.svelte";
	import AvatarStageStrip from "$components/shop/AvatarStageStrip.svelte";
	import { loadAvatarTexture } from "$components/game/three/avatarTexture";
	import { FRAME_COUNT, FRAME_MS, type Loadout } from "$lib/avatar/types";

	let { loadout = null, bot = false }: { loadout: Loadout | null; bot?: boolean } = $props();

	/** WebGL is decided once: a missing context must never mount a Canvas. */
	function detectWebGL(): boolean {
		if (typeof document === "undefined") return false;
		try {
			const canvas = document.createElement("canvas");
			const context = canvas.getContext("webgl2") ?? canvas.getContext("webgl");
			if (!context) return false;
			context.getExtension?.("WEBGL_lose_context")?.loseContext();
			return true;
		} catch {
			return false;
		}
	}

	const webgl = detectWebGL();

	let texture = $state.raw<CanvasTexture | null>(null);
	let frame = $state(0);

	let rafId: number | null = null;
	let lastTime: number | null = null;
	let frameElapsed = 0;
	let reducedMotion = false;

	$effect(() => {
		if (!webgl) return;

		const nextLoadout = loadout;
		const nextBot = bot;
		let cancelled = false;

		loadAvatarTexture(nextLoadout, nextBot)
			.then((loaded) => {
				if (cancelled) {
					loaded.dispose();
					return;
				}
				// INFO: swap in the new strip and retire the one it replaces, so
				// a loadout change never leaks a CanvasTexture.
				texture?.dispose();
				texture = loaded;
			})
			.catch(() => {
				// WARN: a failed bake keeps the previous strip on screen.
			});

		return () => {
			cancelled = true;
		};
	});

	function stepFrames(deltaMs: number): void {
		frameElapsed += deltaMs;
		while (frameElapsed >= FRAME_MS) {
			frameElapsed -= FRAME_MS;
			frame = (frame + 1) % FRAME_COUNT;
		}
	}

	function tick(now: number): void {
		if (document.hidden) {
			rafId = null;
			return;
		}

		if (lastTime === null) lastTime = now;
		const deltaMs = now - lastTime;
		lastTime = now;

		stepFrames(deltaMs);

		rafId = requestAnimationFrame(tick);
	}

	function start(): void {
		if (!webgl || rafId !== null || reducedMotion || document.hidden) return;
		lastTime = null;
		rafId = requestAnimationFrame(tick);
	}

	function stop(): void {
		if (rafId === null) return;
		cancelAnimationFrame(rafId);
		rafId = null;
	}

	function onVisibilityChange(): void {
		if (document.hidden) stop();
		else start();
	}

	onMount(() => {
		reducedMotion = window.matchMedia?.("(prefers-reduced-motion: reduce)").matches ?? false;
		if (!webgl) return;
		document.addEventListener("visibilitychange", onVisibilityChange);
		start();
	});

	onDestroy(() => {
		stop();
		document.removeEventListener("visibilitychange", onVisibilityChange);
		texture?.dispose();
		texture = null;
	});
</script>

<div class="avatar-stage" data-webgl={webgl} data-frame={frame}>
	{#if webgl}
		<Canvas>
			<T.OrthographicCamera makeDefault position={[0, 0, 4]} zoom={170} near={0.1} far={100} />

			<T.Group>
				{#if texture}
					<AvatarStageStrip {texture} {frame} />
				{/if}
			</T.Group>

			<T.Mesh position.y={-0.52} rotation.x={-Math.PI / 2}>
				<T.CircleGeometry args={[0.62, 40]} />
				<T.MeshBasicMaterial color="#7c3aed" transparent opacity={0.28} depthWrite={false} />
			</T.Mesh>
		</Canvas>
	{/if}

	<!-- The fallback sprite is the stage's floor, not its ceiling: it shows
	     whenever there is no WebGL and while the baked strip is still loading,
	     so the stage never reads as an empty box. -->
	{#if !webgl || !texture}
		<div class="avatar-stage-fallback">
			<Avatar {loadout} {bot} size={96} />
		</div>
	{/if}
</div>

<style>
	.avatar-stage {
		position: relative;
		display: grid;
		place-items: center;
		width: 100%;
		height: 100%;
		min-height: 220px;
		overflow: hidden;
	}

	.avatar-stage :global(canvas) {
		display: block;
		width: 100%;
		height: 100%;
	}

	.avatar-stage-fallback {
		position: absolute;
		inset: 0;
		display: grid;
		place-items: center;
		pointer-events: none;
	}
</style>
