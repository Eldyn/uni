<!-- Ambient dust drifting around the table, outside the felt as much as over
     it (dustSeeds.ts thins candidates that land on the mat itself). All
     motion is computed in dust.vert.glsl from each instance's own spawn seed
     plus a continuous uTime — this component builds the instanced geometry
     once per placement change, advances that uniform from the raw frame
     clock, and invalidates every frame.

     Only mounted by Scene3D while storeRenderSettings.ambientDustActive is
     true; this component itself doesn't gate on that setting, so the
     per-frame invalidate below is only ever paid for while dust is actually
     wanted. -->
<script lang="ts">
	import { onDestroy } from "svelte";
	import { T, useTask, useThrelte } from "@threlte/core";
	import * as THREE from "three";
	import { loadTexture } from "./textures";
	import { buildDustVertexShader, buildDustFragmentShader } from "./dust/dustShader";
	import {
		generateDustSeeds,
		DUST_COUNT_HIGH,
		DUST_COUNT_LOW,
		type DustSpawnBounds,
		type DustMatFootprint
	} from "./dust/dustSeeds";
	import { FELT_TEXELS_PER_ART_PIXEL, rippleStepCount, stepIndexAt } from "./ripple/ripplePlan";
	import { matUvToWorld, worldUnitsPerUv } from "./ripple/matRippleGeometry";
	import { storeMatRipple } from "./ripple/matRipple.svelte";
	import { storeWebglCapability } from "$stores/webglCapability.svelte";
	import type { MatPlacement } from "../layout/playmat";
	import type { ViewportInfo } from "../layout/seatLayout";

	let { mat, viewport }: { mat: MatPlacement; viewport: ViewportInfo } = $props();

	// Spawn box extends this far past the mat's own footprint on every side
	// (as a fraction of the mat's half-width/half-height), and spans this
	// height range above the table, in world units.
	const VOID_MARGIN_FRACTION = 0.6;
	const DUST_MIN_HEIGHT = -0.1;
	const DUST_MAX_HEIGHT = 3.2;

	// A single flat quad, ±0.5 in its own local X/Y — dust.vert.glsl scales
	// and billboards it per-instance, so this never changes.
	const QUAD_POSITIONS = new Float32Array([-0.5, -0.5, 0, 0.5, -0.5, 0, 0.5, 0.5, 0, -0.5, 0.5, 0]);
	const QUAD_INDICES = new Uint16Array([0, 1, 2, 0, 2, 3]);

	const dustVertexShader = buildDustVertexShader();
	const dustFragmentShader = buildDustFragmentShader();

	// Built once and mutated in place — never recreated, so the compiled GPU
	// program stays stable across every seed/geometry rebuild below. Disposed
	// once, on this component's own destroy, not per-rebuild.
	const uniforms = {
		uTime: { value: 0 },
		uBlockWorldSize: { value: 1 },
		uRippleOriginWorld: { value: new THREE.Vector2(0, 0) },
		uRippleStep: { value: 0 },
		uRippleElapsed: { value: 0 },
		uRippleMaxRadius: { value: 0 },
		uRippleTotalSteps: { value: 1 },
		uRippleWild: { value: 0 }
	};

	const material = new THREE.ShaderMaterial({
		vertexShader: dustVertexShader,
		fragmentShader: dustFragmentShader,
		uniforms,
		transparent: true,
		depthWrite: false,
		toneMapped: false
	});

	const { invalidate } = useThrelte();

	let geometry = $state<THREE.BufferGeometry | null>(null);
	let instanceCount = $state(0);

	function disposeGeometry(): void {
		geometry?.dispose();
		geometry = null;
	}

	let matSrc = $derived(
		viewport.orientation === "portrait" ? "/assets/mobile_playmat.png" : "/assets/playmat.png"
	);

	// Rebuilds the instanced geometry whenever the mat's own placement or art
	// changes — matSrc/mat feed both the block-size measurement (from the
	// felt texture's own pixel grid) and the spawn box/footprint dustSeeds.ts
	// samples against, so a stale geometry built for a previous placement
	// would spawn dust in the wrong place after a resize or orientation
	// change.
	$effect(() => {
		let cancelled = false;
		loadTexture(matSrc).then((texture) => {
			if (cancelled) return;

			const image = texture.image as { width?: number; height?: number } | undefined;
			const blockWorldSize =
				image?.width && image.width > 0
					? (mat.size[0] * FELT_TEXELS_PER_ART_PIXEL) / image.width
					: mat.size[0] / 64;
			uniforms.uBlockWorldSize.value = blockWorldSize;

			const { left, right, far, near } = mat.bounds;
			const marginX = (right - left) * VOID_MARGIN_FRACTION;
			const marginZ = (near - far) * VOID_MARGIN_FRACTION;
			const bounds: DustSpawnBounds = {
				minX: left - marginX,
				maxX: right + marginX,
				minY: DUST_MIN_HEIGHT,
				maxY: DUST_MAX_HEIGHT,
				minZ: far - marginZ,
				maxZ: near + marginZ
			};
			const matRect: DustMatFootprint = { minX: left, maxX: right, minZ: far, maxZ: near };

			const count = storeWebglCapability.deviceTier === "high" ? DUST_COUNT_HIGH : DUST_COUNT_LOW;
			const seeds = generateDustSeeds(count, bounds, matRect);

			const nextGeometry = new THREE.BufferGeometry();
			nextGeometry.setAttribute("position", new THREE.BufferAttribute(QUAD_POSITIONS, 3));
			nextGeometry.setIndex(new THREE.BufferAttribute(QUAD_INDICES, 1));
			nextGeometry.setAttribute("aSeed", new THREE.InstancedBufferAttribute(seeds, 4));

			disposeGeometry();
			geometry = nextGeometry;
			instanceCount = count;
			invalidate();
		});
		return () => {
			cancelled = true;
		};
	});

	// The wild ripple's uniform data is snapshotted while it runs and kept
	// alive after storeMatRipple clears, so the puff can finish decaying
	// (DUST_WILD_LIFT_SECONDS in dust.vert.glsl, whose continuous decay the
	// last mote only starts at the sweep's final step) instead of snapping
	// flat the instant `active` flips false. `expiresAtMs` bounds the
	// retention: sweep end plus the decay window, kept >= the shader's
	// DUST_WILD_LIFT_SECONDS (0.9s) so the window never truncates a mote
	// that is still visibly decaying.
	const DUST_PUFF_DECAY_MS = 900;

	interface DustRippleSnapshot {
		originX: number;
		originZ: number;
		startTimeMs: number;
		maxRadiusWorld: number;
		totalSteps: number;
		expiresAtMs: number;
	}

	// Ripple-relative step, computed on the CPU with the same
	// stepIndexAt(now - startTimeMs) Playmat3D uses, then handed to the
	// shader as uRippleStep. Deriving it in the shader from the global
	// uTime grid minus a raw start time disagreed by a step depending on the
	// start phase, since uTime is snapped first and the subtraction happened
	// second.
	let rippleSnapshot: DustRippleSnapshot | null = null;

	// Continuous while mounted (dust must not step
	// with the 12fps pixel-look grid — at that rate the screen read as
	// laggy): uTime comes from the raw performance.now() clock and every
	// frame invalidates. Scene3D only mounts this component at all when
	// ambientDustActive is true, which is where the "don't pay for this
	// otherwise" gating lives. The ripple's arrival test still steps at 12fps
	// (uRippleStep), but the puff decay rides the continuous uRippleElapsed.
	useTask(
		() => {
			const now = performance.now();
			uniforms.uTime.value = now / 1000;

			const ripple = storeMatRipple;
			if (ripple.active && ripple.strength === "wild") {
				const [worldX, worldZ] = matUvToWorld(ripple.originUv, mat);
				rippleSnapshot = {
					originX: worldX,
					originZ: worldZ,
					startTimeMs: ripple.startTimeMs,
					maxRadiusWorld: ripple.maxRadius * worldUnitsPerUv(mat),
					totalSteps: rippleStepCount(ripple.durationMs),
					expiresAtMs: ripple.startTimeMs + ripple.durationMs + DUST_PUFF_DECAY_MS
				};
			}
			if (rippleSnapshot !== null && now > rippleSnapshot.expiresAtMs) {
				rippleSnapshot = null;
			}

			if (rippleSnapshot !== null) {
				const elapsedMs = now - rippleSnapshot.startTimeMs;
				uniforms.uRippleOriginWorld.value.set(rippleSnapshot.originX, rippleSnapshot.originZ);
				uniforms.uRippleMaxRadius.value = rippleSnapshot.maxRadiusWorld;
				uniforms.uRippleTotalSteps.value = rippleSnapshot.totalSteps;
				uniforms.uRippleStep.value = stepIndexAt(elapsedMs);
				uniforms.uRippleElapsed.value = Math.max(0, elapsedMs / 1000);
			}
			uniforms.uRippleWild.value = rippleSnapshot !== null ? 1 : 0;

			invalidate();
		},
		{ autoInvalidate: false }
	);

	onDestroy(() => {
		disposeGeometry();
		material.dispose();
	});
</script>

{#if geometry}
	<!-- The vertex shader's own drift/respawn/bob/puff-lift math moves
	     instances well outside the spawn box any bounding volume built from
	     seed positions alone would cover (drift alone reaches ~4.9 units past
	     it at the far end of a cycle) — three's frustum test has no way to
	     see that GPU-side displacement, so it would wrongly cull the whole
	     draw. Disabling it costs one draw of at most 160 quads. -->
	<T.InstancedMesh
		args={[geometry, material, instanceCount]}
		frustumCulled={false}
		dispose={false}
	/>
{/if}
