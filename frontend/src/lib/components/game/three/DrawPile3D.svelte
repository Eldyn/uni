<!-- Fixed decorative draw-pile stack, offset screen-left of the local hand
     (the local hand itself stays a DOM overlay, outside this scene). No
     draw-pile count exists server-side (only pending_draws, a +2/+4 chain
     counter) so this is always the same handful of turned backs; it can't be state-driven. -->
<script lang="ts">
	import { storeGame } from "$stores/game.svelte";
	import CardMesh3D from "./CardMesh3D.svelte";

	const STACK_SIZE = 6;
	const STACK_STEP = 0.008;
	const ANCHOR_X = -1.6;
	const ANCHOR_Z = 3.4;

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
		position={[ANCHOR_X, i * STACK_STEP, ANCHOR_Z]}
		onclick={i === STACK_SIZE - 1 ? handleDraw : undefined}
	/>
{/each}
