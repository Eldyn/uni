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
	// first RichText anywhere, cheap to call from every instance, since
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
			/>
		{:else}
			<!-- Chat text inherits TinyUnicode (see ChatLog/ChatComposer) for near-
			     full unicode coverage; bold/italic apply as plain CSS instead of
			     switching to Pypx/Monogram, which don't cover most alphabets and
			     would break rendering for players typing in other languages. -->
			<span
				style="{segment.bold ? 'font-weight: 800;' : ''}{segment.italic
					? 'font-style: italic;'
					: ''}{segment.color ? `color: ${segment.color};` : ''}"
			>
				{censorText(segment.text)}
			</span>
		{/if}
	{/each}
</span>
