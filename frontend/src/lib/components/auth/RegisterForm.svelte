<script lang="ts">
	import { storeAuth } from "$lib/stores/auth.svelte";
	import FormInput from "$lib/components/common/FormInput.svelte"; // Adjust path if needed

	let { onRegisterSuccess }: { onRegisterSuccess?: () => void } = $props();

	// 1. Grouped State: Much cleaner than 4 separate interfaces
	let form = $state({
		username: "",
		email: "",
		password: "",
		confirmPassword: ""
	});

	let errors = $state({
		username: "",
		email: "",
		password: "",
		confirmPassword: ""
	});

	async function handleSubmit(event: SubmitEvent) {
		event.preventDefault();

		// Clear previous errors
		errors = { username: "", email: "", password: "", confirmPassword: "" };

		const resErrors = await storeAuth.register(
			form.username,
			form.email,
			form.password,
			form.confirmPassword
		);

		// Map errors if any are returned
		if (resErrors && Object.keys(resErrors).length > 0) {
			if (resErrors.username) errors.username = resErrors.username;
			if (resErrors.email) errors.email = resErrors.email;
			if (resErrors.password) errors.password = resErrors.password;
			if (resErrors.confirmPassword) errors.confirmPassword = resErrors.confirmPassword;
			return;
		}

		if (onRegisterSuccess) onRegisterSuccess();
	}
</script>

<form onsubmit={handleSubmit} class="auth-form">
	<h2>Register</h2>

	<FormInput
		id="register-username"
		label="Username:"
		bind:value={form.username}
		error={errors.username}
		placeholder="Choose a username"
		disabled={storeAuth.isLoading}
	/>

	<FormInput
		id="register-email"
		type="email"
		label="Email:"
		bind:value={form.email}
		error={errors.email}
		placeholder="Enter your email"
		disabled={storeAuth.isLoading}
	/>

	<FormInput
		id="register-password"
		type="password"
		label="Password:"
		bind:value={form.password}
		error={errors.password}
		placeholder="Create a strong password"
		disabled={storeAuth.isLoading}
	/>

	<FormInput
		id="register-confirm-password"
		type="password"
		label="Confirm Password:"
		bind:value={form.confirmPassword}
		error={errors.confirmPassword}
		placeholder="Confirm your password"
		disabled={storeAuth.isLoading}
	/>

	<button type="submit" disabled={storeAuth.isLoading} class="submit-button">
		{storeAuth.isLoading ? "Registering..." : "Register"}
	</button>
</form>

<style>
	/* Notice how this file ONLY contains layout styles for the form itself. 
       All the input and label styles are safely inside FormInput.svelte */
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
		margin-top: 8px;
	}

	.submit-button:hover:not(:disabled) {
		opacity: 0.9;
	}

	.submit-button:disabled {
		opacity: 0.6;
		cursor: not-allowed;
	}
</style>
