<script lang="ts">
	interface Props {
		open: boolean;
		title?: string;
		onClose?: () => void;
		size?: 'small' | 'medium' | 'large';
	}

	const { open = false, title, onClose, size = 'medium' }: Props = $props();

	function handleBackdropClick() {
		onClose?.();
	}

	function handleKeydown(e: KeyboardEvent) {
		if (e.key === 'Escape') {
			onClose?.();
		}
	}
</script>

{#if open}
	<div class="modal-backdrop" onclick={handleBackdropClick} onkeydown={handleKeydown} role="presentation">
		<div class="modal modal-{size}" role="dialog" aria-modal="true" onclick={(e) => e.stopPropagation()}>
			<div class="modal-header">
				{#if title}
					<h2 class="modal-title">{title}</h2>
				{/if}
				<button
					type="button"
					class="modal-close"
					onclick={onClose}
					aria-label="Close modal"
				>
					×
				</button>
			</div>
			<div class="modal-content">
				<slot />
			</div>
		</div>
	</div>
{/if}

<style>
	.modal-backdrop {
		position: fixed;
		inset: 0;
		background-color: rgba(0, 0, 0, 0.5);
		display: flex;
		align-items: center;
		justify-content: center;
		z-index: 9000;
		animation: fadeIn 0.2s ease-out;
	}

	@keyframes fadeIn {
		from {
			opacity: 0;
		}
		to {
			opacity: 1;
		}
	}

	.modal {
		background: var(--bg);
		border-radius: 8px;
		box-shadow: var(--shadow);
		max-height: 90vh;
		overflow-y: auto;
		animation: slideUp 0.3s ease-out;
	}

	@keyframes slideUp {
		from {
			transform: translateY(20px);
			opacity: 0;
		}
		to {
			transform: translateY(0);
			opacity: 1;
		}
	}

	.modal-small {
		width: 90%;
		max-width: 300px;
	}

	.modal-medium {
		width: 90%;
		max-width: 500px;
	}

	.modal-large {
		width: 90%;
		max-width: 800px;
	}

	.modal-header {
		display: flex;
		justify-content: space-between;
		align-items: center;
		padding: 20px;
		border-bottom: 1px solid var(--border);
	}

	.modal-title {
		margin: 0;
		font-size: 20px;
		color: var(--text-h);
	}

	.modal-close {
		background: none;
		border: none;
		color: var(--text);
		cursor: pointer;
		font-size: 28px;
		padding: 0;
		width: 32px;
		height: 32px;
		display: flex;
		align-items: center;
		justify-content: center;
		transition: color 0.2s;
	}

	.modal-close:hover {
		color: var(--text-h);
	}

	.modal-content {
		padding: 20px;
	}

	@media (max-width: 1024px) {
		.modal-small {
			max-width: 90vw;
		}

		.modal-medium {
			max-width: 90vw;
		}

		.modal-large {
			max-width: 90vw;
		}
	}
</style>
