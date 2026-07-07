<script lang="ts">
	import { storeToast } from "$stores/toast.svelte";
	import { chatStore } from "$stores/chat.svelte";
	import ToastItem from "$components/common/ToastItem.svelte";

	// Stacks the toast tray above the open chat dock instead of overlapping
	// it — driven by chat's own measured height so this file never needs to
	// know its breakpoints or fixed positioning values.
	const chatOffset = $derived(chatStore.dockHeight > 0 ? chatStore.dockHeight + 12 : 0);
</script>

<div class="toast-container" style="--chat-offset: {chatOffset}px">
	{#each storeToast.items as toast (toast.id)}
		<ToastItem {toast} />
	{/each}
</div>

<style>
	.toast-container {
		position: fixed;
		bottom: calc(24px + var(--chat-offset, 0px));
		right: 24px;
		z-index: 10000;
		max-width: 400px;
		display: flex;
		flex-direction: column;
		gap: 12px;
		pointer-events: none;
	}

	@media (max-width: 1024px) {
		.toast-container {
			bottom: calc(16px + var(--chat-offset, 0px));
			left: 16px;
			right: 16px;
			max-width: 100%;
		}
	}
</style>
