<script lang="ts">
	import FormInput from "$components/common/FormInput.svelte";
	import { storeReset } from "$stores/reset.svelte";
	import { storeI18n } from "$stores/i18n.svelte";
	import { validateEmail } from "$utils/validation";
	import * as m from "$lib/paraglide/messages.js";

	let { onBack }: { onBack?: () => void } = $props();

	let email = $state("");
	let emailError: string | undefined = $state();

	async function handleSubmit(event: SubmitEvent) {
		event.preventDefault();

		const result = validateEmail(email);
		if (!result.valid) {
			emailError = result.error;
			return;
		}
		emailError = undefined;

		await storeReset.requestReset(email.trim());
	}
</script>

{#if storeReset.sent}
	<div class="reset-form auth-form" data-testid="reset-request-sent">
		<p class="reset-message">{m.reset_request_sent({}, { locale: storeI18n.locale })}</p>
		<button type="button" class="btn-ghost pixel-corners back-link" onclick={() => onBack?.()}>
			{m.auth_login_tab({}, { locale: storeI18n.locale })}
		</button>
	</div>
{:else}
	<form onsubmit={handleSubmit} class="reset-form auth-form" data-testid="reset-request-form">
		<h2 class="reset-title font-pixel">
			{m.reset_request_title({}, { locale: storeI18n.locale })}
		</h2>

		<FormInput
			id="reset-email"
			type="email"
			label={m.auth_email_label({}, { locale: storeI18n.locale })}
			bind:value={email}
			error={emailError}
			placeholder={m.auth_email_placeholder({}, { locale: storeI18n.locale })}
			disabled={storeReset.isSending}
			name="email"
			autocomplete="email"
		/>

		<button type="submit" disabled={storeReset.isSending} class="btn pixel-corners">
			{storeReset.isSending ? "..." : m.reset_request_submit({}, { locale: storeI18n.locale })}
		</button>

		<button type="button" class="btn-ghost pixel-corners back-link" onclick={() => onBack?.()}>
			{m.auth_login_tab({}, { locale: storeI18n.locale })}
		</button>
	</form>
{/if}

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

	.reset-message {
		color: var(--text);
		font-family: var(--pixel);
		font-size: 14px;
		line-height: 1.5;
		text-align: center;
		margin: 0;
	}

	.back-link {
		align-self: center;
		font-family: var(--pixel);
		font-size: 13px;
	}
</style>
