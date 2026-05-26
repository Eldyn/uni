<script lang="ts">
	let {
		id,
		label,
		type = "text",
		value = $bindable(""),
		error = "",
		class: className = "",
		...rest
	} = $props();

	let showPassword = $state(false);

	let currentType = $derived(type === "password" && showPassword ? "text" : type);
</script>

<div class="form-group {className}">
	<label for={id}>{label}</label>

	{#if type === "password"}
		<div class="password-input-group">
			<input {id} type={currentType} bind:value class:error={!!error} {...rest} />
			<button
				type="button"
				class="password-toggle"
				onclick={() => (showPassword = !showPassword)}
				disabled={rest.disabled}
				aria-label={showPassword ? "Hide password" : "Show password"}
			>
				{showPassword ? "👁" : "👁‍🗨"}
			</button>
		</div>
	{:else}
		<input {id} type={currentType} bind:value class:error={!!error} {...rest} />
	{/if}

	{#if error}
		<span class="error-text">{error}</span>
	{/if}
</div>

<style>
	.form-group {
		display: flex;
		flex-direction: column;
		gap: 6px;
	}

	label {
		font-size: 14px;
		font-weight: 500;
		color: var(--text-h);
	}

	input {
		padding: 10px 12px;
		border: 2px solid var(--border);
		border-radius: 6px;
		font-size: 14px;
		color: var(--text-h);
		background: var(--bg);
		transition: border-color 0.2s;
	}

	input:focus {
		outline: none;
		border-color: var(--accent);
	}

	input:disabled {
		opacity: 0.6;
		cursor: not-allowed;
	}

	input.error {
		border-color: #dc2626;
	}

	.password-input-group {
		position: relative;
		display: flex;
		align-items: center;
	}

	.password-input-group input {
		flex: 1;
		padding-right: 40px;
	}

	.password-toggle {
		position: absolute;
		right: 10px;
		background: none;
		border: none;
		cursor: pointer;
		font-size: 18px;
		padding: 4px 8px;
		opacity: 0.6;
		transition: opacity 0.2s;
	}

	.password-toggle:hover:not(:disabled) {
		opacity: 1;
	}

	.password-toggle:disabled {
		cursor: not-allowed;
	}

	.error-text {
		font-size: 12px;
		color: #dc2626;
	}
</style>
