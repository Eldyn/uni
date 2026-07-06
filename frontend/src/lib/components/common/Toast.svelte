<script lang="ts">
	import { storeToast, type ToastType } from "../../stores/toast.svelte";

	const PREFIX: Record<ToastType, string> = {
		success: "SUCCESS!",
		error: "ERROR!",
		warning: "WARNING!",
		info: "INFO!"
	};
</script>

<div class="toast-container">
	{#each storeToast.items as toast (toast.id)}
		<div class="toast toast-{toast.type} pixel-corners" role="alert">
			<span class="toast-accent" aria-hidden="true"></span>
			<div class="toast-content">
				<span class="text-base shrink-0">
					{#if toast.type === "success"}
						<i class="hn pix hn-check-circle"></i>
					{:else if toast.type === "error"}
						<i class="hn pix hn-times-circle"></i>
					{:else if toast.type === "warning"}
						<i class="hn pix hn-exclamation-triangle"></i>
					{:else}
						<i class="hn pix hn-info-circle"></i>
					{/if}
				</span>
				<span class="toast-message">
					<span class="toast-prefix">{PREFIX[toast.type]}</span>
					{toast.message}
				</span>
			</div>
			<button
				type="button"
				class="toast-close"
				onclick={() => storeToast.remove(toast.id)}
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
		position: relative;
		display: flex;
		align-items: center;
		justify-content: space-between;
		padding: 12px 16px 12px 22px;
		font-size: 14px;
		background: var(--surface);
		color: var(--text-h);
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

	/* Narrow notched accent bar — same corner-cut technique as the app's
	   pixel-corners/pixel-bordered utilities, scaled down (2px cut vs. their
	   4px) since this strip is only 6px wide. Inset from the toast's own
	   pixel-corners clip so the two notches don't visually collide. */
	.toast-accent {
		position: absolute;
		left: 6px;
		top: 6px;
		bottom: 6px;
		width: 6px;
		clip-path: polygon(
			0 2px,
			2px 2px,
			2px 0,
			100% 0,
			100% 100%,
			2px 100%,
			2px calc(100% - 2px),
			0 calc(100% - 2px)
		);
	}

	.toast-success .toast-accent {
		background: #16a34a;
	}

	.toast-error .toast-accent {
		background: #dc2626;
	}

	.toast-warning .toast-accent {
		background: #f59e0b;
	}

	.toast-info .toast-accent {
		background: #3b82f6;
	}

	.toast-content {
		display: flex;
		align-items: center;
		gap: 12px;
	}

	.toast-message {
		font-family: var(--tiny);
		line-height: 1.5;
	}

	.toast-prefix {
		font-family: var(--pypx);
		font-weight: 800;
		margin-right: 6px;
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
