<!-- Lobby-name search field shared by BrowseToolbar (own inline field, shown
     when the nav renders as a bottom bar) and LobbyBrowse's TopBar search
     slot (shown when the nav renders as a side rail instead). -->
<script lang="ts">
	import { storeI18n } from "$stores/i18n.svelte";
	import * as m from "$lib/paraglide/messages.js";

	let {
		nameQuery = $bindable(),
		compact = false,
		class: className = ""
	}: {
		nameQuery: string;
		/** Smaller text/icon/padding variant used in the TopBar search slot. */
		compact?: boolean;
		class?: string;
	} = $props();
</script>

<div
	class="pixel-bordered flex items-center gap-2 focus-within:[--pc-border:var(--accent)] {compact
		? 'px-4 py-2'
		: 'px-3 py-2'} {className}"
>
	<i class="pia pixelart-icons-font-search {compact ? 'text-sm' : 'text-lg'} text-text"></i>
	<input
		class="w-full min-w-0 bg-transparent font-tiny {compact
			? 'text-sm'
			: 'text-base'} text-text-h outline-none focus-visible:outline-none placeholder:text-text/60"
		placeholder={m.browse_search_placeholder({}, { locale: storeI18n.locale })}
		aria-label="Search lobby name"
		bind:value={nameQuery}
	/>
	{#if nameQuery}
		<button
			class="text-text hover:text-text-h"
			title="Clear search"
			onclick={() => (nameQuery = "")}
			><i class="pia pixelart-icons-font-close {compact ? 'text-xs' : 'text-sm'}"></i></button
		>
	{/if}
</div>
