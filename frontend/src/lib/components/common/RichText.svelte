<script lang="ts">
	import TextEffects from "$components/common/TextEffects.svelte";
	import { parseRichText } from "$utils/richText";
	import { censorText, loadCensorData } from "$utils/censor.svelte";

	let {
		text,
		class: className = ""
	}: {
		text: string;
		class?: string;
	} = $props();

	// Kicks off the (idempotent) lazy load of the profanity word data on the
	// first RichText anywhere — cheap to call from every instance, since
	// loadCensorData() only actually fetches once.
	loadCensorData();

	const segments = $derived(parseRichText(text));
</script>

<span class={className}>
	{#each segments as segment, i (i)}
		{#if segment.effect}
			<TextEffects
				text={censorText(segment.text)}
				effect={segment.effect}
				color={segment.color ?? ""}
				font={segment.bold ? "var(--pypx)" : ""}
			/>
		{:else}
			<span
				style="{segment.bold ? 'font-family: var(--pypx); font-weight: 800;' : ''}{segment.italic
					? 'font-family: var(--monogram); font-style: italic;'
					: ''}{segment.color ? `color: ${segment.color};` : ''}"
			>
				{censorText(segment.text)}
			</span>
		{/if}
	{/each}
</span>
