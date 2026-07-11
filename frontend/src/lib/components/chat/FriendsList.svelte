<script lang="ts">
	import { chatStore, type ChatFriendStatus } from "$stores/chat.svelte";

	let { onselect }: { onselect: () => void } = $props();

	let requestUsername = $state("");

	function pickFriend(friendId: string) {
		chatStore.selectChannel({ friendId });
		onselect();
	}

	function sendRequest() {
		const username = requestUsername.trim();
		if (!username) return;
		void chatStore.requestFriend(username);
		requestUsername = "";
	}

	// Square status marker, same convention as the lobby browse status dot
	// (see joinInfo() in lib/utils/lobbyBrowse.ts) — a Tailwind bg-* class
	// per state, no ad-hoc hex.
	const STATUS_DOT: Record<ChatFriendStatus, string> = {
		offline: "bg-zinc-500",
		online: "bg-green-500"
	};
	const STATUS_LABEL: Record<ChatFriendStatus, string> = {
		offline: "Offline",
		online: "Online"
	};
</script>

<div class="scrollbar-accent flex-1 overflow-y-auto px-2 py-2">
	<div class="flex items-center gap-1 border-b-2 border-border px-1 pb-2">
		<input
			bind:value={requestUsername}
			onkeydown={(e) => e.key === "Enter" && sendRequest()}
			class="min-w-0 flex-1 bg-transparent px-1 font-tiny text-sm text-text-h placeholder:text-text/60"
			placeholder="Add friend by username…"
			aria-label="Add friend by username"
		/>
		<button
			class="pixel-bordered px-2 py-1 font-pixel text-xs uppercase text-white [--pc-border:var(--accent)] [--pc-fill:var(--accent)]"
			onclick={sendRequest}
		>
			Add
		</button>
	</div>

	{#if chatStore.incomingRequests.length > 0}
		<div class="border-b-2 border-border px-1 py-2">
			<p class="mb-1 font-pypx text-xs font-bold uppercase text-text/70">Requests</p>
			{#each chatStore.incomingRequests as username (username)}
				<div class="flex items-center justify-between gap-2 py-1">
					<span class="font-tiny text-sm text-text-h">{username}</span>
					<div class="flex gap-1">
						<button
							class="px-2 py-0.5 font-pixel text-[10px] uppercase text-green-500 hover:text-green-400"
							onclick={() => chatStore.respondToRequest(username, true)}
						>
							Accept
						</button>
						<button
							class="px-2 py-0.5 font-pixel text-[10px] uppercase text-danger hover:text-red-400"
							onclick={() => chatStore.respondToRequest(username, false)}
						>
							Reject
						</button>
					</div>
				</div>
			{/each}
		</div>
	{/if}

	{#each chatStore.friends as friend (friend.username)}
		<button
			class="flex w-full items-center gap-2 px-2 py-2 text-left transition hover:bg-surface"
			onclick={() => pickFriend(friend.username)}
		>
			<span class="h-2 w-2 shrink-0 {STATUS_DOT[friend.status]}" title={STATUS_LABEL[friend.status]}
			></span>
			<span class="sr-only">{STATUS_LABEL[friend.status]}</span>
			<span class="font-tiny text-sm" style="color: {friend.color};">{friend.username}</span>
		</button>
	{/each}
</div>
