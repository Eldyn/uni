<!-- Real accumulating discard pile: renders the entire client-side
     discardHistory (card-bus.svelte.ts) as a scattered stack at the playmat
     center, each card keeping its fixed seeded rotation/jitter (see
     layout/discardPile.ts), newest on top. -->
<script lang="ts">
	import type { DiscardEntry } from "../layout/discardPile";
	import CardMesh3D from "./CardMesh3D.svelte";
	import { EM_TO_WORLD } from "./units";

	let { history }: { history: DiscardEntry[] } = $props();

	const STACK_STEP = 0.006;
</script>

<CardMesh3D card={{ id: -1, type: "wild", value: "0" }} turned={true} position={[0, 0, 0]} />

{#each history as entry, i (entry.card.id)}
	<CardMesh3D
		card={entry.card}
		position={[entry.jitter[0] * EM_TO_WORLD, (i + 1) * STACK_STEP, entry.jitter[1] * EM_TO_WORLD]}
		spinDeg={entry.rotationDeg}
	/>
{/each}
