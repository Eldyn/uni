<!-- Pixel-bordered on/off filter chip: accent fill when active. -->
<script lang="ts">
	import type { Snippet } from "svelte";
	import { storeAudio } from "$stores/audio.svelte";

	let {
		active,
		onclick,
		icon = "",
		title = "",
		class: extraClass = "",
		children
	}: {
		active: boolean;
		onclick: () => void;
		icon?: string;
		title?: string;
		class?: string;
		children: Snippet;
	} = $props();
</script>

<button
	class="pixel-bordered flex items-center gap-2 px-4 py-2 font-tiny text-sm transition-colors {active
		? 'text-white [--pc-border:var(--accent)] [--pc-fill:var(--accent)]'
		: 'text-text-h [--pc-fill:var(--surface-deep)] hover:[--pc-border:var(--accent)]'} {extraClass}"
	aria-pressed={active}
	{title}
	onclick={() => {
		storeAudio.playSfx(active ? "sfx.ui.untick" : "sfx.ui.tick");
		onclick();
	}}
>
	{#if icon}<i class="pia {icon} text-base"></i>{/if}{@render children()}
</button>
