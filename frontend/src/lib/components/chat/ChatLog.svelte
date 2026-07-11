<script lang="ts">
	import { tick } from "svelte";
	import RichText from "$components/common/RichText.svelte";
	import { chatStore, type ChatChannel } from "$stores/chat.svelte";
	import { censorText } from "$utils/censor.svelte";

	let { channel }: { channel: ChatChannel } = $props();

	const lines = $derived(chatStore.linesFor(channel));
	const hasMore = $derived(chatStore.hasMoreHistory(channel));
	const isLoadingMore = $derived(chatStore.isLoadingMoreHistory(channel));

	const friend = $derived(
		typeof channel === "object"
			? chatStore.friends.find((f) => f.username === channel.friendId)
			: undefined
	);

	let containerEl: HTMLDivElement | undefined = $state();
	// Set while loadOlder() is prepending, so the auto-scroll-to-bottom
	// effect below doesn't yank the view down right as older messages load.
	let suppressAutoScroll = false;

	$effect(() => {
		if (lines.length === 0 || suppressAutoScroll) return;
		containerEl?.scrollTo({ top: containerEl.scrollHeight });
	});

	async function loadOlder() {
		if (!containerEl) return;
		const prevHeight = containerEl.scrollHeight;
		suppressAutoScroll = true;
		await chatStore.loadMoreHistory(channel);
		await tick();
		if (containerEl) containerEl.scrollTop = containerEl.scrollHeight - prevHeight;
		suppressAutoScroll = false;
	}
</script>

<div bind:this={containerEl} class="scrollbar-accent flex-1 overflow-y-auto px-3 py-2">
	{#if friend}
		<p class="mb-2 font-tiny text-sm" style="color: {friend.color};">
			Chatting with {friend.username}
		</p>
	{/if}

	{#if channel === "party" && !chatStore.isPartyAvailable}
		<p class="font-tiny text-sm text-text/50">Join a lobby to use party chat.</p>
	{:else if lines.length === 0}
		<p class="font-tiny text-sm text-text/50">No messages yet.</p>
	{:else}
		<div class="flex flex-col gap-1">
			{#if hasMore}
				<button
					class="self-center px-2 py-1 font-pypx text-[10px] uppercase text-text/60 hover:text-text-h disabled:opacity-50"
					disabled={isLoadingMore}
					onclick={loadOlder}
				>
					{isLoadingMore ? "Loading…" : "Load older messages"}
				</button>
			{/if}
			{#each lines as line (line.id)}
				<p class="break-words font-tiny text-sm leading-relaxed text-text">
					<span
						class="uppercase"
						style="font-family: var(--pypx); font-weight: 700; color: {line.color};"
						>{censorText(line.username)}:</span
					>
					<RichText text={line.text} />
				</p>
			{/each}
		</div>
	{/if}
</div>
