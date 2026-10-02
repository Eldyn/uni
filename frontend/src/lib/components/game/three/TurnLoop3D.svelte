<!-- The table's play-direction indicator: a rounded rectangle of pixel
     chevrons and dashes inset from the felt's edge, drawn by a fragment
     shader on a mat-sized plane (same plane, same UVs and art-pixel blocks as
     the felt) so it takes the felt's colour and dithered ripple as if it were
     painted on it. Mounted inside Playmat3D's group, so it inherits the mat's
     offset and quarter turn. Reverse and march behaviour: see
     turnLoopMotion.ts. -->
<script lang="ts">
	import { T, useThrelte } from "@threlte/core";
	import * as THREE from "three";
	import playmatFeltVertexSource from "$lib/shaders/playmatFelt.vert.glsl?raw";
	import { buildTurnLoopFragmentShader } from "./ripple/feltShader";
	import type { MatPlacement, MatSheet } from "../layout/playmat";
	import { fitPitch, loopLength, loopRect, sheetBlockCount } from "./loopGeometry";
	import {
		CHEVRON_HALF_SPREAD_BLOCKS,
		CHEVRON_LENGTH_BLOCKS,
		CHEVRON_STROKE_BLOCKS,
		DASH_CLEARANCE_BLOCKS,
		DASH_HALF_WIDTH_BLOCKS,
		DASH_LENGTH_BLOCKS,
		DASH_PITCH_BLOCKS,
		LOOP_CORNER_RADIUS_BLOCKS,
		LOOP_IDLE_LIGHTEN,
		LOOP_INSET_BLOCKS,
		LOOP_TARGET_PITCH_BLOCKS
	} from "../animation/loopPlan";

	let {
		mat,
		rippleUniforms,
		sheet
	}: {
		mat: MatPlacement;
		/** The felt's own uniform objects: sharing them is how the loop gets the
		 *  felt's colour and ripple with no copying. */
		rippleUniforms: Record<string, THREE.IUniform>;
		/** The sheet showing right now: its size sets the block grid, its felt box
		 *  sets where the loop runs. */
		sheet: MatSheet;
	} = $props();

	// Below the cards' y=0 plane, over the felt (-0.02).
	const LOOP_Y = -0.01;

	const { invalidate } = useThrelte();

	const loopUniforms = {
		uLoopCenter: { value: new THREE.Vector2() },
		uLoopHalfSize: { value: new THREE.Vector2() },
		uLoopCornerRadius: { value: LOOP_CORNER_RADIUS_BLOCKS },
		uLoopLength: { value: 1 },
		uPitch: { value: LOOP_TARGET_PITCH_BLOCKS },
		uPhase: { value: 0 },
		uDirection: { value: 1 },
		uLighten: { value: LOOP_IDLE_LIGHTEN },
		uChevronLength: { value: CHEVRON_LENGTH_BLOCKS },
		uChevronHalfSpread: { value: CHEVRON_HALF_SPREAD_BLOCKS },
		uStroke: { value: CHEVRON_STROKE_BLOCKS },
		uDashLength: { value: DASH_LENGTH_BLOCKS },
		uDashPitch: { value: DASH_PITCH_BLOCKS },
		uDashHalfWidth: { value: DASH_HALF_WIDTH_BLOCKS },
		uDashClearance: { value: DASH_CLEARANCE_BLOCKS }
	};

	// svelte-ignore state_referenced_locally
	const uniforms = { ...rippleUniforms, ...loopUniforms };
	const vertexShader = playmatFeltVertexSource;
	const fragmentShader = buildTurnLoopFragmentShader();

	let direction = $state(1);

	$effect(() => {
		const rect = loopRect(
			sheetBlockCount(sheet),
			sheet.feltUvRect,
			LOOP_INSET_BLOCKS,
			LOOP_CORNER_RADIUS_BLOCKS
		);
		const length = loopLength(rect);
		loopUniforms.uLoopCenter.value.set(rect.centerX, rect.centerY);
		loopUniforms.uLoopHalfSize.value.set(rect.halfWidth, rect.halfHeight);
		loopUniforms.uLoopLength.value = length;
		loopUniforms.uPitch.value = fitPitch(length, LOOP_TARGET_PITCH_BLOCKS);
		invalidate();
	});
</script>

<T.Mesh
	position.y={LOOP_Y}
	rotation.x={-Math.PI / 2}
	data-testid="turn-loop"
	data-direction={direction}
>
	<T.PlaneGeometry args={mat.size} />
	<T.ShaderMaterial
		{vertexShader}
		{fragmentShader}
		{uniforms}
		transparent
		depthWrite={false}
		toneMapped={false}
	/>
</T.Mesh>
