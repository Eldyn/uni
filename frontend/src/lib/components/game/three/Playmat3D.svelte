<!-- The playmat and the turn-direction arrows, as real meshes on the table
     plane instead of DOM sprites layered behind the canvas. They used to be
     full-screen <TintedSprite> siblings of the board, which meant every change
     to the camera's framing had to be mirrored back out into CSS to keep the
     piles sitting on the mat. Here they simply live at the world origin like
     everything else. Tinting works the same way it did in CSS: the texture is
     multiplied by the active color, and the PNG's own alpha is the mask.

     The felt's colour changes sweep through a pixelated, dithered ripple
     instead of snapping instantly — storeMatRipple (ripple/matRipple.svelte.ts)
     owns the from/to colours and the sweep's timing, this component only
     turns that state into shader uniforms every 12fps step. -->
<script lang="ts">
	import { T, useTask, useThrelte } from "@threlte/core";
	import * as THREE from "three";
	import type { Texture } from "three";
	import { storeGame } from "$stores/game.svelte";
	import { storeMatchIntro } from "$stores/matchIntro.svelte";
	import { CARD_COLOR_MAP } from "$lib/palette";
	import { loadTexture } from "./textures";
	import bayerDither from "$lib/shaders/chunks/bayerDither.glsl?raw";
	import playmatFeltVertexSource from "$lib/shaders/playmatFelt.vert.glsl?raw";
	import playmatFeltFragmentSource from "$lib/shaders/playmatFelt.frag.glsl?raw";
	import {
		AMBIENT_STEP_FPS,
		NORMAL_BAND_BLOCKS,
		WILD_BAND_BLOCKS,
		WILD_FLASH_STEPS,
		WILD_FLASH_RADIUS_BLOCKS,
		frontRadiusAt
	} from "./ripple/ripplePlan";
	import { storeMatRipple, MAT_INITIAL_COLOR } from "./ripple/matRipple.svelte";
	import type { MatPlacement } from "../layout/playmat";
	import type { ViewportInfo } from "../layout/seatLayout";

	/** layout/playmat.ts owns the fit, because the hand and the seat ring are
	 *  sized against the felt this draws and all three have to agree on it. The
	 *  arrows share the mat's exact size and offset rather than fitting
	 *  themselves: they are painted on the same 16:9 sheet, so any independent
	 *  fit would slide them off the felt as soon as the two fits disagreed. */
	let {
		mat,
		viewport,
		showFelt = true
	}: {
		mat: MatPlacement;
		viewport: ViewportInfo;
		/** General on/off switch for the felt sheet, kept for any future case
		 *  that wants it hidden — no longer tied to orientation, since portrait
		 *  now has its own dedicated mat art. */
		showFelt?: boolean;
	} = $props();

	// Both sit below the cards' y=0 plane, arrows over the mat.
	const MAT_Y = -0.02;
	const ARROWS_Y = -0.01;

	// The game's four colours, from the one module that defines them —
	// three.js needs real values, not the var() references the DOM side uses.
	const TINTS: Record<string, string> = { ...CARD_COLOR_MAP };

	const ONE_STEP_MS = 1000 / AMBIENT_STEP_FPS;

	// Bayer chunk first, our own precision override next (three.js's own
	// auto-prepended default precision already precedes all of this, but an
	// explicit highp here guarantees the pixel-grid math below — snapping to
	// block centres, gl_FragCoord-driven dithering — doesn't lose integer
	// accuracy on a mediump default). __WILD_FLASH_RADIUS_BLOCKS__ mirrors
	// ripplePlan.ts's constant of the same name, the same way
	// rippleQuad.frag.glsl resolves __MAX_RIPPLES__.
	const feltVertexShader = playmatFeltVertexSource;
	const feltFragmentShader = (
		"precision highp float;\n\n" +
		bayerDither +
		playmatFeltFragmentSource
	).replaceAll("__WILD_FLASH_RADIUS_BLOCKS__", String(WILD_FLASH_RADIUS_BLOCKS));

	// Built once and mutated in place from here on — see the frame task below
	// and the idle-colour effect. Never recreated, so the material identity
	// (and the GPU program compiled for it) stays stable across every colour
	// change and ripple.
	const feltUniforms = {
		uMap: { value: null as Texture | null },
		uFromColor: { value: new THREE.Color(MAT_INITIAL_COLOR) },
		uToColor: { value: new THREE.Color(MAT_INITIAL_COLOR) },
		uOriginUv: { value: new THREE.Vector2(0.5, 0.5) },
		uRadius: { value: 0 },
		uBandBlocks: { value: NORMAL_BAND_BLOCKS },
		uBlockCount: { value: new THREE.Vector2(1, 1) },
		// Real starting value comes from the uAspect-sync effect below, which
		// runs before Threlte's first render — 1 here is just a safe default.
		uAspect: { value: 1 },
		uFlash: { value: 0 },
		uActive: { value: 0 }
	};

	const { invalidate } = useThrelte();

	// active_type reads "white" while a wild is still being resolved; the mat
	// holds the last real color instead of flashing neutral mid-turn.
	$effect(() => {
		// The deal cinematic holds the mat at rebeccapurple regardless of the
		// running game's colour, then normal tinting resumes once it clears.
		if (storeMatchIntro.forcePurpleMat) {
			storeMatRipple.syncColor(MAT_INITIAL_COLOR);
			return;
		}
		const type = storeGame.state?.active_type;
		if (type && type !== "white") storeMatRipple.syncColor(TINTS[type] ?? MAT_INITIAL_COLOR);
	});

	// The arrows plane isn't shader-driven, so it needs a plain reactive
	// colour Svelte can bind straight into MeshBasicMaterial's `color` prop.
	let arrowsTint = $state(MAT_INITIAL_COLOR);

	// Idle sync: whenever the committed colour changes outside an active
	// sweep (a fresh syncColor call above, or a ripple that just finished —
	// #finish() sets committedColor and active=false together), snap both the
	// felt uniforms and the arrows straight to it. While a ripple IS active
	// this is a no-op, so it never fights the per-step updates below.
	$effect(() => {
		const active = storeMatRipple.active;
		const color = storeMatRipple.committedColor;
		if (active) return;
		feltUniforms.uFromColor.value.set(color);
		feltUniforms.uToColor.value.set(color);
		feltUniforms.uFlash.value = 0;
		feltUniforms.uActive.value = 0;
		arrowsTint = color;
		invalidate();
	});

	$effect(() => {
		feltUniforms.uAspect.value = mat.size[1] / mat.size[0];
	});

	let arrowsSrc = $derived(
		(storeGame.state?.play_direction ?? 1) > 0 ? "/assets/cw.png" : "/assets/ccw.png"
	);

	let matTexture = $state<Texture | null>(null);
	let arrowsTexture = $state<Texture | null>(null);

	let matSrc = $derived(
		viewport.orientation === "portrait" ? "/assets/mobile_playmat.png" : "/assets/playmat.png"
	);

	$effect(() => {
		if (!showFelt) return;
		let cancelled = false;
		loadTexture(matSrc).then((t) => {
			if (cancelled) return;
			matTexture = t;
			feltUniforms.uMap.value = t;
			// The felt's own pixel dimensions, so the ripple's blocks align with
			// the art's pixels instead of an arbitrary grid. Defensive against a
			// texture without a decoded image (only happens under test mocks).
			const image = t.image as { width?: number; height?: number } | undefined;
			if (image?.width && image?.height) {
				feltUniforms.uBlockCount.value.set(image.width, image.height);
			}
			invalidate();
		});
		return () => {
			cancelled = true;
		};
	});

	$effect(() => {
		let cancelled = false;
		loadTexture(arrowsSrc).then((t) => {
			if (!cancelled) arrowsTexture = t;
		});
		return () => {
			cancelled = true;
		};
	});

	// Tracks which sweep is currently driving the uniforms, so a brand new
	// ripple starting on the same 12fps step index as the previous one's last
	// step still forces an update instead of being mistaken for "unchanged".
	let currentSweepStart = -1;
	let lastStepIndex = -1;

	// Zero per-frame work while idle: the callback still runs every real
	// frame (on-demand rendering only skips the actual GPU render, not the
	// task loop), but it returns immediately unless a ripple is active, and
	// even then only touches uniforms/invalidates when the 12fps step index
	// actually changes. Completion (active -> inactive) is handled by the
	// idle-colour effect above, not here.
	useTask(
		() => {
			const ripple = storeMatRipple;
			if (!ripple.active) return;

			if (ripple.startTimeMs !== currentSweepStart) {
				currentSweepStart = ripple.startTimeMs;
				lastStepIndex = -1;
			}

			const elapsed = performance.now() - ripple.startTimeMs;
			const stepIndex = Math.floor(elapsed / ONE_STEP_MS);
			if (stepIndex === lastStepIndex) return;
			lastStepIndex = stepIndex;

			feltUniforms.uFromColor.value.set(ripple.fromColor);
			feltUniforms.uToColor.value.set(ripple.toColor);
			feltUniforms.uOriginUv.value.set(ripple.originUv.u, ripple.originUv.v);
			feltUniforms.uBandBlocks.value =
				ripple.strength === "wild" ? WILD_BAND_BLOCKS : NORMAL_BAND_BLOCKS;
			feltUniforms.uFlash.value =
				ripple.strength === "wild" && stepIndex < WILD_FLASH_STEPS ? 1 : 0;
			feltUniforms.uActive.value = 1;

			const radius = frontRadiusAt(elapsed, ripple.durationMs, ripple.maxRadius);
			feltUniforms.uRadius.value = radius;
			arrowsTint = radius >= ripple.maxRadius / 2 ? ripple.toColor : ripple.fromColor;

			invalidate();
		},
		{ autoInvalidate: false }
	);
</script>

<T.Group
	position.x={mat.offsetX}
	position.z={mat.offsetZ}
	rotation.y={mat.quarterTurn ? Math.PI / 2 : 0}
>
	{#if showFelt && matTexture}
		<T.Mesh position.y={MAT_Y} rotation.x={-Math.PI / 2}>
			<T.PlaneGeometry args={mat.size} />
			<T.ShaderMaterial
				vertexShader={feltVertexShader}
				fragmentShader={feltFragmentShader}
				uniforms={feltUniforms}
				transparent
				depthWrite={false}
				toneMapped={false}
			/>
		</T.Mesh>
	{/if}

	{#if arrowsTexture}
		<T.Mesh position.y={ARROWS_Y} rotation.x={-Math.PI / 2}>
			<T.PlaneGeometry args={mat.size} />
			<T.MeshBasicMaterial
				map={arrowsTexture}
				color={arrowsTint}
				transparent
				depthWrite={false}
				toneMapped={false}
			/>
		</T.Mesh>
	{/if}
</T.Group>
