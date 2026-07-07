<script lang="ts">
	import { storeAuth } from "$stores/auth.svelte";
	import FormInput from "$components/common/FormInput.svelte";

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
	<FormInput
		id="login-email"
		label="Email:"
		bind:value={email}
		error={emailError}
		placeholder="Enter your email"
		disabled={storeAuth.isLoading}
		name="email"
		autocomplete="email"
	/>

	<FormInput
		id="login-password"
		type="password"
		label="Password:"
		bind:value={password}
		error={passwordError}
		placeholder="Enter your password"
		disabled={storeAuth.isLoading}
		name="password"
		autocomplete="current-password"
	/>

	<button type="submit" disabled={storeAuth.isLoading} class="btn pixel-corners">
		{storeAuth.isLoading ? "Logging in..." : "Login"}
	</button>
</form>

<style>
	.auth-form {
		display: flex;
		flex-direction: column;
		gap: 16px;
	}
</style>
