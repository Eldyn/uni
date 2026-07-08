<script lang="ts">
	import { storeToast } from "$stores/toast.svelte";
	import ToastItem from "$components/common/ToastItem.svelte";
</script>

<div class="toast-container">
	{#each storeToast.items as toast (toast.id)}
		<ToastItem {toast} />
	{/each}
</div>

<style>
	.toast-container {
		position: fixed;
		bottom: 24px;
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
			bottom: 16px;
			left: 16px;
			right: 16px;
			max-width: 100%;
		}
	}
</style>
