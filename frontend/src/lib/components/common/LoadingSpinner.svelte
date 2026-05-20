<script lang="ts">
	interface Props {
		size?: 'small' | 'medium' | 'large';
		text?: string;
	}

	const { size = 'medium', text }: Props = $props();
</script>

<div class="spinner-container spinner-{size}">
	<div class="spinner"></div>
	{#if text}
		<p class="spinner-text">{text}</p>
	{/if}
</div>

<style>
	.spinner-container {
		display: flex;
		flex-direction: column;
		align-items: center;
		justify-content: center;
		gap: 12px;
	}

	.spinner {
		border: 3px solid var(--accent-bg);
		border-top-color: var(--accent);
		border-radius: 50%;
		animation: spin 0.8s linear infinite;
	}

	@keyframes spin {
		to {
			transform: rotate(360deg);
		}
	}

	.spinner-small {
		--size: 24px;
	}

	.spinner-medium {
		--size: 40px;
	}

	.spinner-large {
		--size: 60px;
	}

	.spinner {
		width: var(--size);
		height: var(--size);
	}

	.spinner-text {
		color: var(--text);
		font-size: 14px;
		margin: 0;
	}
</style>
