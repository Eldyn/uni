<script lang="ts">
	import ChatChannelTabs from "$components/chat/ChatChannelTabs.svelte";
	import ChatLog from "$components/chat/ChatLog.svelte";
	import ChatComposer from "$components/chat/ChatComposer.svelte";
	import FriendsList from "$components/chat/FriendsList.svelte";
	import { chatStore } from "$stores/chat.svelte";
	import { storeAuth } from "$stores/auth.svelte";
	import { acceleratorKey } from "$lib/actions/keyboardAccelerators";
	import { storeI18n } from "$stores/i18n.svelte";
	import * as m from "$lib/paraglide/messages.js";

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
			class="chat-panel pixel-bordered fixed inset-x-0 bottom-0 z-50 flex h-[70svh] flex-col [--pc-border:var(--accent)] md:inset-x-auto md:bottom-4 md:right-4 md:h-[28rem] md:w-96"
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
			class="chat-launcher pixel-corners fixed bottom-4 right-4 z-50 flex h-12 w-12 items-center justify-center bg-accent text-white"
			title={m.chat_dock_open({}, { locale: storeI18n.locale })}
			aria-label={m.chat_dock_open({}, { locale: storeI18n.locale })}
			aria-keyshortcuts={acceleratorKey("chat")}
			onclick={open}
		>
			<svg
				viewBox="0 0 24 24"
				width="1.25em"
				height="1.25em"
				fill="currentColor"
				aria-hidden="true"
				><path
					d="M22 22h-2v-2h-2v-2h2v-8h2v12Zm-4-4h-8v-2h8v2ZM4 12h2v2H4v2H2V4h2v8Zm6 4H8v-6h2v6Zm10-6H10V8h10v2Zm-4-4h-2V4h2v2Zm-2-2H4V2h10v2Z"
				></path></svg
			>
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

<style>
	/* On the bottom-bar breakpoint (narrow width + tall viewport, the same
	   condition under which NavBar/ShellFrame render the bottom nav bar
	   instead of a side rail), lift the launcher above it so it doesn't
	   overlap the rightmost "Menu" destination. Above that breakpoint, or
	   whenever the rail is showing instead, the nav sits to the side rather
	   than along the bottom, so the default bottom-4 placement is fine. */
	.chat-launcher {
		bottom: calc(1rem + 62px);
	}

	@media (min-width: 768px), (max-height: 599px) {
		.chat-launcher {
			bottom: 1rem;
		}
	}

	/* Mobile landscape: a rotated phone is wide enough to match `md:`, so the
	   dock would otherwise take that branch's 28rem-tall floating panel — taller
	   than the viewport itself. The bottom-sheet branch is no better here, since
	   70svh of a short viewport leaves the composer squeezed against the log.
	   A right-edge drawer fits the shape the rotation actually gives us: full
	   height, and only as wide as the conversation needs. Unlayered, so it wins
	   over the Tailwind utilities on the same element regardless of their order.
	   Above `lg` the desktop floating panel is correct again. */
	@media (max-width: 1023.98px) and (orientation: landscape) {
		.chat-panel {
			inset: 0 0 0 auto;
			width: min(22rem, 55vw);
			height: 100svh;
		}
	}
</style>
