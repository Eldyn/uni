<script lang="ts">
	import FormInput from "$components/common/FormInput.svelte";
	import { storeReset } from "$stores/reset.svelte";
	import { storeI18n } from "$stores/i18n.svelte";
	import { validatePassword, validatePasswordMatch } from "$utils/validation";
	import * as m from "$lib/paraglide/messages.js";

	let {
		token,
		onResetSuccess,
		onBack
	}: { token: string; onResetSuccess?: () => void; onBack?: () => void } = $props();

	let password = $state("");
	let confirmPassword = $state("");
	let passwordError: string | undefined = $state();
	let confirmError: string | undefined = $state();

	async function handleSubmit(event: SubmitEvent) {
		event.preventDefault();

		passwordError = undefined;
		confirmError = undefined;

		const passwordResult = validatePassword(password);
		if (!passwordResult.valid) {
			passwordError = passwordResult.error;
			return;
		}

		const matchResult = validatePasswordMatch(password, confirmPassword);
		if (!matchResult.valid) {
			confirmError = matchResult.error;
			return;
		}

		const success = await storeReset.confirmReset(token, password);
		if (success && onResetSuccess) {
			onResetSuccess();
		}
	}
</script>

<form onsubmit={handleSubmit} class="reset-form auth-form" data-testid="reset-password-form">
	<h2 class="reset-title font-pixel">{m.reset_title({}, { locale: storeI18n.locale })}</h2>

	<FormInput
		id="reset-new-password"
		type="password"
		label={m.reset_new_password_label({}, { locale: storeI18n.locale })}
		bind:value={password}
		error={passwordError}
		disabled={storeReset.isConfirming}
		name="new-password"
		autocomplete="new-password"
	/>

	<FormInput
		id="reset-confirm-password"
		type="password"
		label={m.reset_confirm_password_label({}, { locale: storeI18n.locale })}
		bind:value={confirmPassword}
		error={confirmError}
		disabled={storeReset.isConfirming}
		name="confirm-password"
		autocomplete="new-password"
	/>

	{#if storeReset.error}
		<p class="error-message text-danger" role="alert">{storeReset.error}</p>
	{/if}

	<button type="submit" disabled={storeReset.isConfirming} class="btn pixel-corners">
		{storeReset.isConfirming ? "..." : m.reset_submit({}, { locale: storeI18n.locale })}
	</button>

	{#if onBack}
		<button type="button" class="btn-ghost pixel-corners back-link" onclick={() => onBack?.()}>
			{m.auth_login_tab({}, { locale: storeI18n.locale })}
		</button>
	{/if}
</form>

<style>
	.reset-form {
		display: flex;
		flex-direction: column;
		gap: 16px;
	}

	.reset-title {
		font-size: 16px;
		color: var(--text-h);
		text-align: center;
		margin: 0;
	}

	.error-message {
		font-size: 12px;
		text-align: center;
		margin: 0;
	}

	.back-link {
		align-self: center;
		font-size: 13px;
	}
</style>
