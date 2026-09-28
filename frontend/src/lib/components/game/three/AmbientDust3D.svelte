<!-- Ambient dust drifting around the table, outside the felt as much as over
     it (dustSeeds.ts thins candidates that land on the mat itself). All
     motion is computed in dust.vert.glsl from each instance's own spawn seed
     plus a 12fps-quantized uTime — this component only builds the instanced
     geometry once per placement change and steps that one uniform.

     Only mounted by Scene3D while storeRenderSettings.ambientDustActive is
     true; this component itself doesn't gate on that setting, so the
     continuous 12fps invalidate below is only ever paid for while dust is
     actually wanted. -->
<script lang="ts">
	import { onDestroy } from "svelte";
	import { T, useTask, useThrelte } from "@threlte/core";
	import * as THREE from "three";
	import { loadTexture } from "./textures";
	import dustVertexSource from "$lib/shaders/dust.vert.glsl?raw";
	import dustFragmentSource from "$lib/shaders/dust.frag.glsl?raw";
	import {
		generateDustSeeds,
		DUST_COUNT_HIGH,
		DUST_COUNT_LOW,
		type DustSpawnBounds,
		type DustMatFootprint
	} from "./dust/dustSeeds";
	import { AMBIENT_STEP_MS, FELT_TEXELS_PER_ART_PIXEL, stepIndexAt } from "./ripple/ripplePlan";
	import { matUvToWorld, rippleFrontSpeedWorldPerMs } from "./ripple/matRippleGeometry";
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

	const dustVertexShader = dustVertexSource;
	const dustFragmentShader = "precision mediump float;\n\n" + dustFragmentSource;

	// Built once and mutated in place — never recreated, so the compiled GPU
	// program stays stable across every seed/geometry rebuild below. Disposed
	// once, on this component's own destroy, not per-rebuild.
	const uniforms = {
		uTime: { value: 0 },
		uBlockWorldSize: { value: 1 },
		uRippleOriginWorld: { value: new THREE.Vector2(0, 0) },
		uRippleStartTime: { value: 0 },
		uRippleSpeed: { value: 0 },
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

			// Fixed bounding sphere covering the whole spawn box, so frustum
			// culling never has to (and never does) recompute one from the
			// quad's own tiny local-space vertices.
			const center = new THREE.Vector3(
				(bounds.minX + bounds.maxX) / 2,
				(bounds.minY + bounds.maxY) / 2,
				(bounds.minZ + bounds.maxZ) / 2
			);
			const radius = Math.hypot(
				(bounds.maxX - bounds.minX) / 2,
				(bounds.maxY - bounds.minY) / 2,
				(bounds.maxZ - bounds.minZ) / 2
			);
			nextGeometry.boundingSphere = new THREE.Sphere(center, radius);

			disposeGeometry();
			geometry = nextGeometry;
			instanceCount = count;
			invalidate();
		});
		return () => {
			cancelled = true;
		};
	});

	let lastStepIndex = -1;

	// Continuous while mounted: dust always drifts, ripple or not, so this
	// invalidates every 12fps step regardless of ripple state — Scene3D only
	// mounts this component at all when ambientDustActive is true, which is
	// where the "don't pay for this otherwise" gating lives.
	useTask(
		() => {
			const stepIndex = stepIndexAt(performance.now());
			if (stepIndex === lastStepIndex) return;
			lastStepIndex = stepIndex;

			uniforms.uTime.value = (stepIndex * AMBIENT_STEP_MS) / 1000;

			const ripple = storeMatRipple;
			const wild = ripple.active && ripple.strength === "wild";
			uniforms.uRippleWild.value = wild ? 1 : 0;
			if (wild) {
				const [worldX, worldZ] = matUvToWorld(ripple.originUv, mat);
				uniforms.uRippleOriginWorld.value.set(worldX, worldZ);
				uniforms.uRippleStartTime.value = ripple.startTimeMs / 1000;
				uniforms.uRippleSpeed.value =
					rippleFrontSpeedWorldPerMs(ripple.maxRadius, ripple.durationMs, mat) * 1000;
			}

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
	<T.InstancedMesh args={[geometry, material, instanceCount]} frustumCulled dispose={false} />
{/if}
