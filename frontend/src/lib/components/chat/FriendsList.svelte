<script lang="ts">
	import { chatStore, type ChatFriendStatus } from "$stores/chat.svelte";
	import { storeAuth } from "$stores/auth.svelte";
	import { storeNavigation } from "$stores/navigation.svelte";
	import { storeI18n } from "$stores/i18n.svelte";
	import * as m from "$lib/paraglide/messages.js";

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
	// (see joinInfo() in lib/utils/lobbyBrowse.ts), a Tailwind bg-* class
	// per state, no ad-hoc hex.
	const STATUS_DOT: Record<ChatFriendStatus, string> = {
		offline: "bg-zinc-500",
		online: "bg-green-500"
	};

	function statusLabel(status: ChatFriendStatus): string {
		return status === "online"
			? m.chat_friends_status_online({}, { locale: storeI18n.locale })
			: m.chat_friends_status_offline({}, { locale: storeI18n.locale });
	}
</script>

{#if storeAuth.isGuest}
	<div class="flex flex-1 flex-col items-center justify-center gap-3 px-4 text-center">
		<p class="font-tiny text-sm text-text/70">
			{m.chat_friends_guest_notice({}, { locale: storeI18n.locale })}
		</p>
		<button
			class="pixel-bordered px-4 py-2 font-pixel text-xs uppercase text-white [--pc-border:var(--accent)] [--pc-fill:var(--accent)]"
			onclick={() => storeNavigation.gotoAuth("register")}
		>
			{m.chat_friends_guest_register({}, { locale: storeI18n.locale })}
		</button>
	</div>
{:else}
	<div class="scrollbar-accent flex-1 overflow-y-auto px-2 py-2">
		<div class="flex items-center gap-1 border-b-2 border-border px-1 pb-2">
			<input
				bind:value={requestUsername}
				onkeydown={(e) => e.key === "Enter" && sendRequest()}
				class="min-w-0 flex-1 bg-transparent px-1 font-tiny text-sm text-text-h placeholder:text-text/60"
				placeholder={m.chat_friends_search_placeholder({}, { locale: storeI18n.locale })}
				aria-label={m.chat_friends_search_aria({}, { locale: storeI18n.locale })}
			/>
			<button
				class="pixel-bordered px-2 py-1 font-pixel text-xs uppercase text-white [--pc-border:var(--accent)] [--pc-fill:var(--accent)]"
				onclick={sendRequest}
			>
				{m.chat_friends_add_button({}, { locale: storeI18n.locale })}
			</button>
		</div>

		{#if chatStore.incomingRequests.length > 0}
			<div class="border-b-2 border-border px-1 py-2">
				<p class="mb-1 font-pypx text-xs font-bold uppercase text-text/70">
					{m.chat_friends_requests_heading({}, { locale: storeI18n.locale })}
				</p>
				{#each chatStore.incomingRequests as username (username)}
					<div class="flex items-center justify-between gap-2 py-1">
						<span class="font-tiny text-sm text-text-h">{username}</span>
						<div class="flex gap-1">
							<button
								class="px-2 py-0.5 font-pixel text-[10px] uppercase text-green-500 hover:text-green-400"
								onclick={() => chatStore.respondToRequest(username, true)}
							>
								{m.chat_friends_accept({}, { locale: storeI18n.locale })}
							</button>
							<button
								class="px-2 py-0.5 font-pixel text-[10px] uppercase text-danger hover:text-red-400"
								onclick={() => chatStore.respondToRequest(username, false)}
							>
								{m.chat_friends_reject({}, { locale: storeI18n.locale })}
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
				<span
					class="h-2 w-2 shrink-0 {STATUS_DOT[friend.status]}"
					title={statusLabel(friend.status)}
				></span>
				<span class="sr-only">{statusLabel(friend.status)}</span>
				<span class="font-tiny text-sm" style="color: {friend.color};">{friend.username}</span>
			</button>
		{/each}
	</div>
{/if}
