<script lang="ts">
	import { chatStore } from "$stores/chat.svelte";
	import { storeNavigation } from "$stores/navigation.svelte";

	let {
		showFriendsList = $bindable(),
		onclose
	}: { showFriendsList: boolean; onclose: () => void } = $props();

	const partyLabel = $derived(storeNavigation.current === "game" ? "GAME" : "PARTY");

	function selectGlobal() {
		showFriendsList = false;
		chatStore.selectChannel("global");
	}
	function selectParty() {
		showFriendsList = false;
		chatStore.selectChannel("party");
	}
	function selectFriends() {
		showFriendsList = true;
	}

	const isGlobalActive = $derived(!showFriendsList && chatStore.activeChannel === "global");
	const isPartyActive = $derived(!showFriendsList && chatStore.activeChannel === "party");
</script>

{#snippet tab(label: string, active: boolean, onclick: () => void)}
	<button
		{onclick}
		class="flex flex-1 items-center justify-center px-2 py-2 font-pypx text-xs font-extrabold uppercase transition {active
			? 'bg-accent text-white'
			: 'text-text hover:text-text-h'}"
	>
		{label}
	</button>
{/snippet}

<div class="flex items-stretch border-b-2 border-border">
	{@render tab("GLOBAL", isGlobalActive, selectGlobal)}
	{@render tab(partyLabel, isPartyActive, selectParty)}
	{@render tab("FRIENDS", showFriendsList, selectFriends)}
	<button
		class="flex items-center justify-center px-3 text-text hover:text-text-h"
		title="Close chat"
		onclick={onclose}
		aria-label="Close chat"
	>
		<i class="hn pix hn-times"></i>
	</button>
</div>
