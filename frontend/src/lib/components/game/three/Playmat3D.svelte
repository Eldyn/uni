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
	import type { MatPlacement } from "../layout/playmat";

	/** layout/playmat.ts owns the fit, because the hand and the seat ring are
	 *  sized against the felt this draws and all three have to agree on it. The
	 *  arrows share the mat's exact size and offset rather than fitting
	 *  themselves: they are painted on the same 16:9 sheet, so any independent
	 *  fit would slide them off the felt as soon as the two fits disagreed. */
	let {
		mat,
		showFelt = true
	}: {
		mat: MatPlacement;
		/** Portrait drops the felt sheet — a table players visually "sit on top
		 *  of" reads wrong on a phone-sized board — while keeping the turn-
		 *  direction arrows, which are their own texture/mesh, not baked into
		 *  the felt's. */
		showFelt?: boolean;
	} = $props();

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
		if (!showFelt) return;
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

</script>

{#if showFelt && matTexture}
	<T.Mesh position.y={MAT_Y} position.z={mat.offsetZ} rotation.x={-Math.PI / 2}>
		<T.PlaneGeometry args={mat.size} />
		<T.MeshBasicMaterial
			map={matTexture}
			color={activeTint}
			transparent
			depthWrite={false}
			toneMapped={false}
		/>
	</T.Mesh>
{/if}

{#if arrowsTexture}
	<T.Mesh position.y={ARROWS_Y} position.z={mat.offsetZ} rotation.x={-Math.PI / 2}>
		<T.PlaneGeometry args={mat.size} />
		<T.MeshBasicMaterial
			map={arrowsTexture}
			color={activeTint}
			transparent
			depthWrite={false}
			toneMapped={false}
		/>
	</T.Mesh>
{/if}
