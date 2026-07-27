<!-- Fixed decorative draw-pile stack, anchored just left of the local hand and
     on the same horizontal (Z) line as it — not floating above. No draw-pile
     count exists server-side (only pending_draws, a +2/+4 chain counter) so
     this is always the same handful of turned backs; it can't be state-driven. -->
<script lang="ts">
	import { storeGame } from "$stores/game.svelte";
	import CardMesh3D from "./CardMesh3D.svelte";
	import type { BoardPlacement } from "../layout/boardPlacement";

	let { placement }: { placement: BoardPlacement } = $props();

	const STACK_SIZE = 6;
	// Same z-fighting fix as the other piles/hands: keep the step clear of
	// CardMesh3D's own internal layer span (up to 0.004 world units).
	const STACK_STEP = 0.02;
	// Each deeper back peeks out a little above the one below it, so the pile
	// reads as an actual stack of cards instead of a single lone back. Peeking
	// toward the mat (−Z) rather than the viewer keeps the pile's own near
	// edge flush with the bottom edge of the local hand row instead of
	// drooping past it.
	const PILE_PEEK_Z = 0.07;

	function handleDraw() {
		if (storeGame.state?.current_turn === storeGame.localPlayer?.username) {
			storeGame.drawCard();
		}
	}
</script>

{#each Array.from({ length: STACK_SIZE }) as _, i (i)}
	<CardMesh3D
		card={{ id: -1, type: "wild", value: "0" }}
		turned={true}
		position={[placement.drawPileX, i * STACK_STEP, placement.localSeatZ - i * PILE_PEEK_Z]}
		scale={placement.handScale}
		onclick={i === STACK_SIZE - 1 ? handleDraw : undefined}
	/>
{/each}
