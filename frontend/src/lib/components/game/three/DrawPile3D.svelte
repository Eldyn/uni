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
	import {
		MAX_VISIBLE_STACK,
		DRAW_PILE_STACK_STEP as STACK_STEP,
		PILE_BASE_HEIGHT,
		PILE_PEEK_Z,
		visibleDrawPileStackSize
	} from "../layout/drawPile";

	let { placement }: { placement: BoardPlacement } = $props();

	let visibleStackSize = $derived(visibleDrawPileStackSize(storeGame.state?.draw_pile_size ?? 0));
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

{#each Array.from({ length: visibleStackSize }) as _, i (i)}
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
		onclick={i === visibleStackSize - 1 ? handleDraw : undefined}
	/>
{/each}
