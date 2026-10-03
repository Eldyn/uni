<script lang="ts">
	import { storeGame } from "$stores/game.svelte";
	import SchemaFallbackPrompt from "./SchemaFallbackPrompt.svelte";
	import HeldDrawPrompt from "./HeldDrawPrompt.svelte";
	import { kindRegistry } from "./kindRegistry";

	let prompt = $derived(storeGame.activePrompt);
	let PromptComponent = $derived(
		prompt ? (kindRegistry[prompt.kind] ?? SchemaFallbackPrompt) : null
	);
</script>

{#if !storeGame.isSpectator && prompt && PromptComponent}
	<PromptComponent {prompt} />
{/if}

<HeldDrawPrompt />
