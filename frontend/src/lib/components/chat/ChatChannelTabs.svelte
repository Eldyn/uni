<script lang="ts">
	import { chatStore } from "$stores/chat.svelte";
	import { storeNavigation } from "$stores/navigation.svelte";
	import { storeI18n } from "$stores/i18n.svelte";
	import * as m from "$lib/paraglide/messages.js";

	let {
		showFriendsList = $bindable(),
		onclose
	}: { showFriendsList: boolean; onclose: () => void } = $props();

	const partyLabel = $derived(
		storeNavigation.current === "game"
			? m.chat_tab_game({}, { locale: storeI18n.locale })
			: m.chat_tab_party({}, { locale: storeI18n.locale })
	);

	function selectGlobal() {
		showFriendsList = false;
		chatStore.selectChannel("global");
	}
	function selectParty() {
		if (!chatStore.isPartyAvailable) return;
		showFriendsList = false;
		chatStore.selectChannel("party");
	}
	function selectFriends() {
		showFriendsList = true;
	}

	const isGlobalActive = $derived(!showFriendsList && chatStore.activeChannel === "global");
	const isPartyActive = $derived(!showFriendsList && chatStore.activeChannel === "party");
</script>

{#snippet tab(label: string, active: boolean, onclick: () => void, disabled: boolean = false)}
	<button
		{onclick}
		{disabled}
		title={disabled
			? m.chat_tab_party_disabled_tooltip({}, { locale: storeI18n.locale })
			: undefined}
		class="flex flex-1 items-center justify-center px-2 py-2 font-pypx text-xs font-extrabold uppercase transition {active
			? 'bg-accent text-white'
			: disabled
				? 'text-text/30'
				: 'text-text hover:text-text-h'}"
	>
		{label}
	</button>
{/snippet}

<div class="flex items-stretch border-b-2 border-border">
	{@render tab(m.chat_tab_global({}, { locale: storeI18n.locale }), isGlobalActive, selectGlobal)}
	{@render tab(partyLabel, isPartyActive, selectParty, !chatStore.isPartyAvailable)}
	{@render tab(
		m.chat_tab_friends({}, { locale: storeI18n.locale }),
		showFriendsList,
		selectFriends
	)}
	<button
		class="flex items-center justify-center px-3 text-text hover:text-text-h"
		title={m.chat_dock_close({}, { locale: storeI18n.locale })}
		onclick={onclose}
		aria-label={m.chat_dock_close({}, { locale: storeI18n.locale })}
	>
		<i class="pia pixelart-icons-font-close"></i>
	</button>
</div>
