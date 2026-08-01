<!-- The playmat and the turn-direction arrows, as real meshes on the table
     plane instead of DOM sprites layered behind the canvas. They used to be
     full-screen <TintedSprite> siblings of the board, which meant every change
     to the camera's framing had to be mirrored back out into CSS to keep the
     piles sitting on the mat. Here they simply live at the world origin like
     everything else. Tinting works the same way it did in CSS: the texture is
     multiplied by the active color, and the PNG's own alpha is the mask. -->
<script lang="ts">
	import { T } from "@threlte/core";
	import type { Texture } from "three";
	import { storeGame } from "$stores/game.svelte";
	import { CARD_COLOR_MAP } from "$lib/palette";
	import { loadTexture } from "./textures";
	import { coverSize } from "../layout/playmat";
	import type { CameraRig } from "../layout/cameraRig";

	let { rig }: { rig: CameraRig } = $props();

	// Both sit below the cards' y=0 plane, arrows over the mat.
	const MAT_Y = -0.02;
	const ARROWS_Y = -0.01;

	// The game's four colours, from the one module that defines them —
	// three.js needs real values, not the var() references the DOM side uses.
	const TINTS: Record<string, string> = { ...CARD_COLOR_MAP };
	const FALLBACK_TINT = "#663399";

	// active_type reads "white" while a wild is still being resolved; the mat
	// holds the last real color instead of flashing neutral mid-turn.
	let activeTint = $state(FALLBACK_TINT);
	$effect(() => {
		const type = storeGame.state?.active_type;
		if (type && type !== "white") activeTint = TINTS[type] ?? FALLBACK_TINT;
	});

	let arrowsSrc = $derived(
		(storeGame.state?.play_direction ?? 1) > 0 ? "/assets/cw.png" : "/assets/ccw.png"
	);

	let matTexture = $state<Texture | null>(null);
	let arrowsTexture = $state<Texture | null>(null);

	$effect(() => {
		let cancelled = false;
		loadTexture("/assets/playmat.png").then((t) => {
			if (!cancelled) matTexture = t;
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

	/** layout/playmat.ts owns the fit, because the hand and the seat ring are
	 *  sized against the felt this draws and all three have to agree on it. */
	function fit(texture: Texture, halfWidth: number, halfHeight: number): [number, number] {
		const image = texture.image as { width: number; height: number };
		return coverSize(image.width, image.height, halfWidth, halfHeight);
	}

	let matSize = $derived(matTexture ? fit(matTexture, rig.halfWidth, rig.halfHeight) : null);
	let arrowsSize = $derived(
		arrowsTexture ? fit(arrowsTexture, rig.halfWidth, rig.halfHeight) : null
	);
</script>

{#if matTexture && matSize}
	<T.Mesh position.y={MAT_Y} rotation.x={-Math.PI / 2}>
		<T.PlaneGeometry args={matSize} />
		<T.MeshBasicMaterial
			map={matTexture}
			color={activeTint}
			transparent
			depthWrite={false}
			toneMapped={false}
		/>
	</T.Mesh>
{/if}

{#if arrowsTexture && arrowsSize}
	<T.Mesh position.y={ARROWS_Y} rotation.x={-Math.PI / 2}>
		<T.PlaneGeometry args={arrowsSize} />
		<T.MeshBasicMaterial
			map={arrowsTexture}
			color={activeTint}
			transparent
			depthWrite={false}
			toneMapped={false}
		/>
	</T.Mesh>
{/if}
