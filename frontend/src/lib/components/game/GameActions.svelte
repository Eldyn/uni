<script lang="ts">
	import { storeGame } from "$stores/game.svelte";
	import ActionPickColor from "./actions/ActionPickColor.svelte";
	import ActionPlayDrawn from "./actions/ActionPlayDrawn.svelte";
	import ActionChooseTarget from "./actions/ActionChooseTarget.svelte";
</script>

{#if !storeGame.isSpectator}
	<ActionPlayDrawn />
	<ActionPickColor />
	<ActionChooseTarget />
{/if}
