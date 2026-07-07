<script lang="ts">
	import { chatStore } from "$stores/chat.svelte";
	import type { ChatFriendStatus } from "$data/chatMock";

	let { onselect }: { onselect: () => void } = $props();

	function pickFriend(friendId: string) {
		chatStore.selectChannel({ friendId });
		onselect();
	}

	// Square status marker, same convention as the lobby browse status dot
	// (see joinInfo() in lib/utils/lobbyBrowse.ts) — a Tailwind bg-* class
	// per state, no ad-hoc hex.
	const STATUS_DOT: Record<ChatFriendStatus, string> = {
		offline: "bg-zinc-500",
		online: "bg-green-500",
		"in-game": "bg-accent"
	};
	const STATUS_LABEL: Record<ChatFriendStatus, string> = {
		offline: "Offline",
		online: "Online",
		"in-game": "In a game"
	};
</script>

<div class="scrollbar-accent flex-1 overflow-y-auto px-2 py-2">
	{#each chatStore.friends as friend (friend.id)}
		<button
			class="flex w-full items-center gap-2 px-2 py-2 text-left transition hover:bg-surface"
			onclick={() => pickFriend(friend.id)}
		>
			<span class="h-2 w-2 shrink-0 {STATUS_DOT[friend.status]}" title={STATUS_LABEL[friend.status]}
			></span>
			<span class="sr-only">{STATUS_LABEL[friend.status]}</span>
			<span class="font-tiny text-sm" style="color: {friend.color};">{friend.name}</span>
		</button>
	{/each}
</div>
