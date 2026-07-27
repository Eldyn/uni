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
	import { loadTexture } from "./textures";
	import type { CameraRig } from "../layout/cameraRig";

	let { rig }: { rig: CameraRig } = $props();

	// Both sit below the cards' y=0 plane, arrows over the mat.
	const MAT_Y = -0.02;
	const ARROWS_Y = -0.01;

	// The same palette GameScreen.svelte's :root defines for the DOM board —
	// three.js needs real values, not var() references.
	const TINTS: Record<string, string> = {
		red: "#dc251c",
		yellow: "#fcf604",
		blue: "#0493de",
		green: "#018d41",
		black: "#1f1b18"
	};
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

	/** "cover" fit, exactly as the CSS background-size it replaces: scale by
	 *  whichever axis needs more, so the art always fills the frustum. */
	function coverSize(texture: Texture, halfWidth: number, halfHeight: number): [number, number] {
		const image = texture.image as { width: number; height: number };
		const scale = Math.max((halfWidth * 2) / image.width, (halfHeight * 2) / image.height);
		return [image.width * scale, image.height * scale];
	}

	let matSize = $derived(matTexture ? coverSize(matTexture, rig.halfWidth, rig.halfHeight) : null);
	let arrowsSize = $derived(
		arrowsTexture ? coverSize(arrowsTexture, rig.halfWidth, rig.halfHeight) : null
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
