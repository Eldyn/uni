<!-- The 3D avatar stage: a Threlte orthographic canvas showing the baked strip
     on a textured plane, with a ground glow disc under it. Dragging turns the
     plane around Y within +/-35 degrees and eases it back on release; the
     strip steps through its 5 frames on the shared 120 ms clock, stopping
     while the tab is hidden. Without WebGL the plain Avatar is the fallback,
     so the preview never blocks the screen. -->
<script lang="ts">
	import { onDestroy, onMount } from "svelte";
	import { Canvas, T } from "@threlte/core";
	import type { CanvasTexture } from "three";
	import Avatar from "$components/common/Avatar.svelte";
	import { loadAvatarTexture } from "$components/game/three/avatarTexture";
	import { FRAME_COUNT, FRAME_MS, type Loadout } from "$lib/avatar/types";

	let { loadout = null, bot = false }: { loadout: Loadout | null; bot?: boolean } = $props();

	const MAX_ANGLE = 35;
	const DEGREES_PER_PX = 0.35;

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
	let angle = $state(0);

	let dragging = false;
	let dragStartX = 0;
	let dragStartAngle = 0;

	let rafId: number | null = null;
	let lastTime: number | null = null;
	let frameElapsed = 0;
	let reducedMotion = false;

	const rotationY = $derived((angle * Math.PI) / 180);

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

	// The strip's frame cursor is its own effect: reading `frame` inside the
	// load effect above would re-run the bake on every animation frame.
	$effect(() => {
		if (texture) texture.offset.x = frame / FRAME_COUNT;
	});

	function stepFrames(deltaMs: number): void {
		frameElapsed += deltaMs;
		while (frameElapsed >= FRAME_MS) {
			frameElapsed -= FRAME_MS;
			frame = (frame + 1) % FRAME_COUNT;
			if (texture) texture.offset.x = frame / FRAME_COUNT;
		}
	}

	function easeAngle(deltaMs: number): void {
		if (dragging || angle === 0) return;
		const step = 1 - Math.exp(-deltaMs / 90);
		angle *= 1 - step;
		if (Math.abs(angle) < 0.1) angle = 0;
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
		easeAngle(deltaMs);

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

	function onPointerDown(event: PointerEvent): void {
		if (!webgl) return;
		dragging = true;
		dragStartX = event.clientX;
		dragStartAngle = angle;
		(event.currentTarget as HTMLElement).setPointerCapture?.(event.pointerId);
	}

	function onPointerMove(event: PointerEvent): void {
		if (!dragging) return;
		const delta = (event.clientX - dragStartX) * DEGREES_PER_PX;
		angle = Math.max(-MAX_ANGLE, Math.min(MAX_ANGLE, dragStartAngle + delta));
	}

	function onPointerUp(): void {
		dragging = false;
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

<!-- svelte-ignore a11y_no_static_element_interactions -- the whole stage is
     the drag surface: the pointer handlers are the grab, not a control -->
<div
	class="avatar-stage"
	data-webgl={webgl}
	data-frame={frame}
	data-angle={Math.round(angle)}
	onpointerdown={onPointerDown}
	onpointermove={onPointerMove}
	onpointerup={onPointerUp}
	onpointercancel={onPointerUp}
	onpointerleave={onPointerUp}
>
	{#if webgl}
		<Canvas>
			<T.OrthographicCamera makeDefault position={[0, 0, 4]} zoom={170} near={0.1} far={100} />

			<T.Group rotation.y={rotationY}>
				{#if texture}
					<T.Mesh position.y={0.04}>
						<T.PlaneGeometry args={[1, 1]} />
						<T.MeshBasicMaterial
							map={texture}
							transparent
							alphaTest={0.05}
							depthWrite={false}
							toneMapped={false}
						/>
					</T.Mesh>
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
		touch-action: none;
		cursor: grab;
	}

	.avatar-stage:active {
		cursor: grabbing;
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
