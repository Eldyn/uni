<script lang="ts">
	import ChatChannelTabs from "$components/chat/ChatChannelTabs.svelte";
	import ChatLog from "$components/chat/ChatLog.svelte";
	import ChatComposer from "$components/chat/ChatComposer.svelte";
	import FriendsList from "$components/chat/FriendsList.svelte";
	import { chatStore } from "$stores/chat.svelte";
	import { storeAuth } from "$stores/auth.svelte";

	let showFriendsList = $state(false);
	let panelHeight = $state(0);

	const unreadBadge = $derived(
		chatStore.totalUnread > 9
			? "9+"
			: chatStore.totalUnread > 0
				? String(chatStore.totalUnread)
				: ""
	);

	// Keeps chatStore.dockHeight in sync with the panel's actual rendered
	// height while open, and resets it to 0 as soon as the dock closes,
	// since the bound element unmounts on close and can't report 0 itself.
	$effect(() => {
		if (!chatStore.isOpen) return;
		chatStore.dockHeight = panelHeight;
		return () => {
			chatStore.dockHeight = 0;
		};
	});

	function open() {
		chatStore.open();
	}

	function close() {
		chatStore.close();
	}

	function onFriendSelected() {
		showFriendsList = false;
	}
</script>

{#if storeAuth.isLoggedIn || storeAuth.isGuest}
	{#if chatStore.isOpen}
		<div
			bind:clientHeight={panelHeight}
			class="pixel-bordered fixed inset-x-0 bottom-0 z-50 flex h-[70vh] flex-col [--pc-border:var(--accent)] md:inset-x-auto md:bottom-4 md:right-4 md:h-[28rem] md:w-96"
		>
			<ChatChannelTabs bind:showFriendsList onclose={close} />

			{#if showFriendsList}
				<FriendsList onselect={onFriendSelected} />
			{:else}
				<ChatLog channel={chatStore.activeChannel} />
			{/if}

			{#if !showFriendsList}
				<ChatComposer />
			{/if}
		</div>
	{:else}
		<button
			class="pixel-bordered fixed bottom-4 right-4 z-50 flex h-12 w-12 items-center justify-center bg-accent text-white [--pc-border:var(--accent)] [--pc-fill:var(--accent)]"
			title="Open chat"
			aria-label="Open chat"
			onclick={open}
		>
			<i class="hn pix hn-message-dots text-xl"></i>
			{#if unreadBadge}
				<span
					class="absolute -right-1 -top-1 flex h-5 min-w-5 items-center justify-center rounded-full bg-danger px-1 font-mono text-[10px] leading-none text-white ring-2 ring-bg"
				>
					{unreadBadge}
				</span>
			{/if}
		</button>
	{/if}
{/if}
