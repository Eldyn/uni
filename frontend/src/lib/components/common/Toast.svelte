<script lang="ts">
	import { getUIState, removeToast } from '../../stores/ui.svelte';

	const uiState = getUIState();
</script>

<div class="toast-container">
	{#each uiState.toasts as toast (toast.id)}
		<div class="toast toast-{toast.type}" role="alert">
			<div class="toast-content">
				<span class="toast-icon">
					{#if toast.type === 'success'}
						✓
					{:else if toast.type === 'error'}
						✕
					{:else if toast.type === 'warning'}
						⚠
					{:else}
						ℹ
					{/if}
				</span>
				<span class="toast-message">{toast.message}</span>
			</div>
			<button
				type="button"
				class="toast-close"
				onclick={() => removeToast(toast.id)}
				aria-label="Close notification"
			>
				×
			</button>
		</div>
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

	.toast {
		display: flex;
		align-items: center;
		justify-content: space-between;
		padding: 12px 16px;
		border-radius: 6px;
		font-size: 14px;
		box-shadow: var(--shadow);
		animation: slideIn 0.3s ease-out;
		pointer-events: auto;
		max-width: 400px;
	}

	@keyframes slideIn {
		from {
			transform: translateX(400px);
			opacity: 0;
		}
		to {
			transform: translateX(0);
			opacity: 1;
		}
	}

	.toast-success {
		background-color: #dcfce7;
		color: #166534;
		border-left: 4px solid #16a34a;
	}

	.toast-error {
		background-color: #fee2e2;
		color: #991b1b;
		border-left: 4px solid #dc2626;
	}

	.toast-warning {
		background-color: #fef3c7;
		color: #92400e;
		border-left: 4px solid #f59e0b;
	}

	.toast-info {
		background-color: #dbeafe;
		color: #0c2d6b;
		border-left: 4px solid #3b82f6;
	}

	.toast-content {
		display: flex;
		align-items: center;
		gap: 12px;
	}

	.toast-icon {
		font-weight: bold;
		font-size: 16px;
		flex-shrink: 0;
	}

	.toast-message {
		line-height: 1.5;
	}

	.toast-close {
		background: none;
		border: none;
		color: inherit;
		cursor: pointer;
		font-size: 20px;
		padding: 0;
		margin-left: 12px;
		opacity: 0.7;
		transition: opacity 0.2s;
		flex-shrink: 0;
	}

	.toast-close:hover {
		opacity: 1;
	}

	@media (max-width: 1024px) {
		.toast-container {
			bottom: 16px;
			right: 16px;
			left: 16px;
			max-width: 100%;
		}

		.toast {
			max-width: 100%;
		}
	}
</style>
