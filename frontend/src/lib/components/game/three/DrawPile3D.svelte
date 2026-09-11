<!-- Fixed decorative draw-pile stack, anchored just left of the local hand and
     on the same horizontal (Z) line as it — not floating above. No draw-pile
     count exists server-side (only pending_draws, a +2/+4 chain counter) so
     this is always the same handful of turned backs; it can't be state-driven. -->
<script lang="ts">
	import { T } from "@threlte/core";
	import { storeGame } from "$stores/game.svelte";
	import CardMesh3D from "./CardMesh3D.svelte";
	import { loadSilhouette } from "./textures";
	import { CARD_WIDTH, CARD_HEIGHT } from "./units";
	import type { BoardPlacement } from "../layout/boardPlacement";

	let { placement }: { placement: BoardPlacement } = $props();

	const STACK_SIZE = 6;
	// Same z-fighting fix as the other piles/hands: keep the step clear of
	// CardMesh3D's own internal layer span (up to 0.004 world units).
	const STACK_STEP = 0.02;
	// Lifts the whole pile clear of the local hand's own height range — a
	// resting hand card sits at i*STACK_STEP (LocalHand3D), which grows with
	// hand size, and a hovered/dragged one jumps to CARD_HOVER_LIFT (0.5); this
	// base has to clear both so the pile can never end up underneath a card
	// whose XZ footprint happens to overlap it.
	const PILE_BASE_HEIGHT = 0.6;
	// Each deeper back peeks out a little above the one below it, so the pile
	// reads as an actual stack of cards instead of a single lone back. Peeking
	// toward the mat (−Z) rather than the viewer keeps the pile's own near
	// edge flush with the bottom edge of the local hand row instead of
	// drooping past it. Kept to a hairline on purpose — just enough of each
	// card's own bottom edge to read as "another sheet", not enough to show
	// its face.
	const PILE_PEEK_Z = 0.02;
	// A thin dark silhouette peeking a hair further than each card sells the
	// "stacked sheets" cue — the same idea as the shadow under a hand card
	// (LocalHand3D's own `shadow` prop), just offset along the stack's own
	// peek axis (Z) instead of a hand row's horizontal one.
	const SHADOW_PEEK_Z = 0.006;
	const SHADOW_DROP_Y = STACK_STEP / 2;
	const SHADOW_OPACITY = 0.3;

	let silhouetteTexture = $state<import("three").Texture | null>(null);
	$effect(() => {
		let cancelled = false;
		loadSilhouette("/assets/cards/background.png").then((t) => {
			if (!cancelled) silhouetteTexture = t;
		});
		return () => {
			cancelled = true;
		};
	});

	function handleDraw() {
		if (storeGame.state?.current_turn === storeGame.localPlayer?.username) {
			storeGame.drawCard();
		}
	}
</script>

{#each Array.from({ length: STACK_SIZE }) as _, i (i)}
	{#if i > 0 && silhouetteTexture}
		<T.Mesh
			position={[
				placement.drawPileX,
				PILE_BASE_HEIGHT + i * STACK_STEP - SHADOW_DROP_Y,
				placement.drawPileZ - ((i - 1) * PILE_PEEK_Z + SHADOW_PEEK_Z) * placement.drawPileScale
			]}
			rotation.x={-Math.PI / 2}
			scale={placement.drawPileScale}
		>
			<T.PlaneGeometry args={[CARD_WIDTH, CARD_HEIGHT]} />
			<T.MeshBasicMaterial
				map={silhouetteTexture}
				color="#000000"
				transparent
				depthWrite
				opacity={SHADOW_OPACITY}
				toneMapped={false}
			/>
		</T.Mesh>
	{/if}
	<CardMesh3D
		card={{ id: -1, type: "wild", value: "0" }}
		turned={true}
		position={[
			placement.drawPileX,
			PILE_BASE_HEIGHT + i * STACK_STEP,
			placement.drawPileZ - i * PILE_PEEK_Z * placement.drawPileScale
		]}
		scale={placement.drawPileScale}
		onclick={i === STACK_SIZE - 1 ? handleDraw : undefined}
	/>
{/each}
