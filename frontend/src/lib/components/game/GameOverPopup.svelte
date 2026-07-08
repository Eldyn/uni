<script lang="ts">
	import { storeGame } from "$stores/game.svelte";
	import GameVictoryPopup from "./popup/GameVictoryPopup.svelte";
	import GameInterruptedPopup from "./popup/GameInterruptedPopup.svelte";

	let isVictory = $derived(!!storeGame.state?.winner);
	let isInterrupted = $derived(storeGame.state?.is_over && !storeGame.state?.winner);
</script>

{#if storeGame.state?.is_over}
	{#if isInterrupted}
		<GameInterruptedPopup />
	{:else if isVictory}
		<GameVictoryPopup />
	{/if}
{/if}
