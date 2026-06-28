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

<div class="flex flex-col gap-1.5 {className}">
	<label for={id} class="font-pixel text-text-h" style="font-size: 14px; font-weight: 500;"
		>{label}</label
	>

	{#if type === "password"}
		<div class="password-input-group">
			<input
				{id}
				type={currentType}
				class="input-pixel"
				bind:value
				class:error={!!error}
				{...rest}
			/>
			<button
				type="button"
				class="password-toggle"
				onclick={() => (showPassword = !showPassword)}
				disabled={rest.disabled}
				aria-label={showPassword ? "Hide password" : "Show password"}
			>
				<i class="hn pix {showPassword ? 'hn-eye-cross' : 'hn-eye'}"></i>
			</button>
		</div>
	{:else}
		<input {id} type={currentType} class="input-pixel" bind:value class:error={!!error} {...rest} />
	{/if}

	{#if error}
		<span class="text-danger" style="font-size: 12px;">{error}</span>
	{/if}
</div>

<style>
	.password-input-group {
		position: relative;
		display: flex;
		align-items: center;
	}

	.password-input-group :global(.input-pixel) {
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
</style>
