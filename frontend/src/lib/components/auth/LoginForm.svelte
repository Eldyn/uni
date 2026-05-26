<script lang="ts">
	import { storeAuth } from "$lib/stores/auth.svelte";
	import FormInput from "../common/FormInput.svelte";

	let { onLoginSuccess }: { onLoginSuccess: () => void } = $props();

	let email = $state("");
	let emailError: string | undefined = $state();

	let password = $state("");
	let passwordError: string | undefined = $state();
	let showPassword = $state(false);

	async function handleSubmit(event: SubmitEvent) {
		event.preventDefault();

		const errors = await storeAuth.login(email, password);

		if (Object.keys(errors).length > 0) {
			emailError = errors.email;
			passwordError = errors.password;
			return;
		}

		onLoginSuccess();
	}
</script>

<form onsubmit={handleSubmit} class="auth-form">
	<h2>Login</h2>

	<FormInput
		id="login-email"
		label="Email:"
		bind:value={email}
		error={emailError}
		placeholder="Enter your email"
		disabled={storeAuth.isLoading}
	/>

	<FormInput
		id="login-password"
		type="password"
		label="Password:"
		bind:value={password}
		error={passwordError}
		placeholder="Enter your password"
		disabled={storeAuth.isLoading}
	/>

	<button type="submit" disabled={storeAuth.isLoading} class="submit-button">
		{storeAuth.isLoading ? "Logging in..." : "Login"}
	</button>
</form>

<style>
	.auth-form {
		display: flex;
		flex-direction: column;
		gap: 16px;
	}

	h2 {
		margin: 0 0 16px;
		font-size: 20px;
		color: var(--text-h);
	}

	.submit-button {
		padding: 10px 16px;
		background: var(--accent);
		color: white;
		border: none;
		border-radius: 6px;
		font-size: 14px;
		font-weight: 500;
		cursor: pointer;
		transition: opacity 0.2s;
	}

	.submit-button:hover:not(:disabled) {
		opacity: 0.9;
	}

	.submit-button:disabled {
		opacity: 0.6;
		cursor: not-allowed;
	}
</style>
