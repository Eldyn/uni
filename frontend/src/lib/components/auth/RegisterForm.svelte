<script lang="ts">
	import { storeAuth } from "$stores/auth.svelte";
	import FormInput from "$components/common/FormInput.svelte";
	import { censorText, loadCensorData } from "$utils/censor.svelte";

	let { onRegisterSuccess }: { onRegisterSuccess?: () => void } = $props();

	// Warm the censor data as soon as the register form mounts, so the
	// username check on submit (below) doesn't wait on the fetch.
	loadCensorData();

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

		errors = { username: "", email: "", password: "", confirmPassword: "" };

		await loadCensorData();
		if (censorText(form.username) !== form.username) {
			errors.username = "Please choose a different username.";
			return;
		}

		const resErrors = await storeAuth.register(
			form.username,
			form.email,
			form.password,
			form.confirmPassword
		);

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
	<FormInput
		id="register-username"
		label="Username:"
		bind:value={form.username}
		error={errors.username}
		placeholder="Choose a username"
		disabled={storeAuth.isLoading}
		name="username"
		autocomplete="username"
	/>

	<FormInput
		id="register-email"
		type="email"
		label="Email:"
		bind:value={form.email}
		error={errors.email}
		placeholder="Enter your email"
		disabled={storeAuth.isLoading}
		name="email"
		autocomplete="email"
	/>

	<FormInput
		id="register-password"
		type="password"
		label="Password:"
		bind:value={form.password}
		error={errors.password}
		placeholder="Create a strong password"
		disabled={storeAuth.isLoading}
		name="password"
		autocomplete="new-password"
	/>

	<FormInput
		id="register-confirm-password"
		type="password"
		label="Confirm Password:"
		bind:value={form.confirmPassword}
		error={errors.confirmPassword}
		placeholder="Confirm your password"
		disabled={storeAuth.isLoading}
		name="confirm-password"
		autocomplete="new-password"
	/>

	<button type="submit" disabled={storeAuth.isLoading} class="btn pixel-corners">
		{storeAuth.isLoading ? "Registering..." : "Register"}
	</button>
</form>

<style>
	.auth-form {
		display: flex;
		flex-direction: column;
		gap: 16px;
	}
</style>
