<script lang="ts">
	import { storeAuth } from "$stores/auth.svelte";
	import FormInput from "$components/common/FormInput.svelte";
	import { storeI18n } from "$stores/i18n.svelte";
	import * as m from "$lib/paraglide/messages.js";

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
		label={m.auth_email_label({}, { locale: storeI18n.locale })}
		bind:value={email}
		error={emailError}
		placeholder={m.auth_email_placeholder({}, { locale: storeI18n.locale })}
		disabled={storeAuth.isLoading}
		name="email"
		autocomplete="email"
	/>

	<FormInput
		id="login-password"
		type="password"
		label={m.auth_password_label({}, { locale: storeI18n.locale })}
		bind:value={password}
		error={passwordError}
		placeholder={m.auth_password_placeholder({}, { locale: storeI18n.locale })}
		disabled={storeAuth.isLoading}
		name="password"
		autocomplete="current-password"
	/>

	<button type="submit" disabled={storeAuth.isLoading} class="btn pixel-corners">
		{storeAuth.isLoading
			? m.auth_logging_in({}, { locale: storeI18n.locale })
			: m.auth_submit_login({}, { locale: storeI18n.locale })}
	</button>
</form>

<style>
	.auth-form {
		display: flex;
		flex-direction: column;
		gap: 16px;
	}
</style>
