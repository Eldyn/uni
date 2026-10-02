<!-- The table's play-direction indicator: a rounded rectangle of pixel
     chevrons and dashes inset from the felt's edge, drawn by a fragment
     shader on a mat-sized plane (same plane, same UVs and art-pixel blocks as
     the felt) so it takes the felt's colour and dithered ripple as if it were
     painted on it. Mounted inside Playmat3D's group, so it inherits the mat's
     offset and quarter turn. Reverse and march behaviour: see
     turnLoopMotion.ts. -->
<script lang="ts">
	import { onDestroy, untrack } from "svelte";
	import { T, useTask, useThrelte } from "@threlte/core";
	import * as THREE from "three";
	import { storeAnimation } from "$stores/animation.svelte";
	import { storeDirectionRing } from "$stores/directionRing.svelte";
	import { storeGame } from "$stores/game.svelte";
	import { storeRenderSettings } from "$stores/renderSettings.svelte";
	import playmatFeltVertexSource from "$lib/shaders/playmatFelt.vert.glsl?raw";
	import { buildTurnLoopFragmentShader } from "./ripple/feltShader";
	import type { DirectionSign } from "../animation/directionRing";
	import type { MatPlacement, MatSheet } from "../layout/playmat";
	import { fitLoopRect, loopLength, loopRect, sheetBlockCount } from "./loopGeometry";
	import { TurnLoopMotion } from "./turnLoopMotion";
	import {
		CHEVRON_ARM_BLOCKS,
		CHEVRON_STROKE_BLOCKS,
		LOOP_CELL_BLOCKS,
		LOOP_CHEVRON_CLEAR_SLOTS,
		LOOP_CORNER_RADIUS_BLOCKS,
		LOOP_DASH_BLOCKS,
		LOOP_DASH_THICKNESS_BLOCKS,
		LOOP_INSET_BLOCKS,
		LOOP_MAX_CORNER_RADIUS_BLOCKS,
		LOOP_MIN_CORNER_RADIUS_BLOCKS,
		LOOP_SLOTS_PER_CELL,
		LOOP_SLOT_BLOCKS
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

	let direction = $state<DirectionSign>(1);

	const motion = new TurnLoopMotion({
		motionActive: () => storeRenderSettings.matRippleActive,
		speedMultiplier: () => storeAnimation.speedMultiplier,
		onChange: () => {
			direction = motion.direction.value as DirectionSign;
			invalidate();
		}
	});

	const loopUniforms = {
		uLoopCenter: { value: new THREE.Vector2() },
		uLoopHalfSize: { value: new THREE.Vector2() },
		uLoopCornerRadius: { value: LOOP_CORNER_RADIUS_BLOCKS },
		uLoopLength: { value: 1 },
		uPhase: motion.phase,
		uDirection: motion.direction,
		uTone: motion.tone,
		uSlot: { value: LOOP_SLOT_BLOCKS },
		uDashLength: { value: LOOP_DASH_BLOCKS },
		uDashThickness: { value: LOOP_DASH_THICKNESS_BLOCKS },
		uSlotsPerCell: { value: LOOP_SLOTS_PER_CELL },
		uChevronClearSlots: { value: LOOP_CHEVRON_CLEAR_SLOTS },
		uChevronArm: { value: CHEVRON_ARM_BLOCKS },
		uChevronStroke: { value: CHEVRON_STROKE_BLOCKS }
	};

	// svelte-ignore state_referenced_locally
	const uniforms = { ...rippleUniforms, ...loopUniforms };
	const vertexShader = playmatFeltVertexSource;
	const fragmentShader = buildTurnLoopFragmentShader();

	$effect(() => {
		const nominal = loopRect(
			sheetBlockCount(sheet),
			sheet.feltUvRect,
			LOOP_INSET_BLOCKS,
			LOOP_CORNER_RADIUS_BLOCKS
		);
		const rect = fitLoopRect(nominal, {
			cellLength: LOOP_CELL_BLOCKS,
			preferredCornerRadius: LOOP_CORNER_RADIUS_BLOCKS,
			minCornerRadius: LOOP_MIN_CORNER_RADIUS_BLOCKS,
			maxCornerRadius: LOOP_MAX_CORNER_RADIUS_BLOCKS
		});
		const length = loopLength(rect);
		motion.loopLength = length;
		loopUniforms.uLoopCenter.value.set(rect.centerX, rect.centerY);
		loopUniforms.uLoopHalfSize.value.set(rect.halfWidth, rect.halfHeight);
		loopUniforms.uLoopCornerRadius.value = rect.cornerRadius;
		loopUniforms.uLoopLength.value = length;
		invalidate();
	});

	storeDirectionRing.set(storeGame.state?.play_direction ?? 1);
	motion.snapTo(storeDirectionRing.sign);

	let lastFlipToken = storeDirectionRing.token;
	let lastSettleToken = storeDirectionRing.settleToken;

	$effect(() => {
		const token = storeDirectionRing.token;
		if (token === lastFlipToken) return;
		lastFlipToken = token;
		const flips = storeDirectionRing.takeFlips();
		untrack(() => motion.enqueue(flips));
	});

	$effect(() => {
		const token = storeDirectionRing.settleToken;
		if (token === lastSettleToken) return;
		lastSettleToken = token;
		untrack(() => motion.snapTo(storeDirectionRing.sign));
	});

	// INFO: turning motion off mid-flip must end the brighten at once, or the
	//       loop stays lit with nothing left to drive it.
	$effect(() => {
		if (storeRenderSettings.matRippleActive) return;
		untrack(() => motion.motionStopped());
	});

	// INFO: the march runs on real frames, not the ambient 12fps grid: the
	//       old ring quantised its spin to that grid and it aliased into stutter.
	useTask((delta) => motion.tick(delta), { autoInvalidate: false });

	onDestroy(() => motion.dispose());
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
