<script lang="ts">
	/**
	 * @file ProfileAccount.svelte
	 * @brief Account body for the owner's profile: email status line and the
	 * verify action. Logout lives in ProfileLogout (the header action).
	 */
	import { storeAuth } from "$stores/auth.svelte";
	import { storeNavigation } from "$stores/navigation.svelte";
	import { storeI18n } from "$stores/i18n.svelte";
	import * as m from "$lib/paraglide/messages.js";
	import VerifyCodeForm from "$components/auth/VerifyCodeForm.svelte";

	let showVerify = $state(Boolean(storeNavigation.activeVerifyCode));
	$effect(() => {
		if (storeNavigation.activeVerifyCode) {
			showVerify = true;
		}
	});

	const options = $derived({ locale: storeI18n.locale });

	const emailStatus = $derived(
		storeAuth.emailVerified
			? m.profile_email_verified({}, options)
			: m.profile_email_unverified({}, options)
	);
</script>

{#if storeAuth.isLoggedIn}
	<div class="profile-account">
		<p class="email-status">{emailStatus}</p>
		{#if !storeAuth.emailVerified}
			<button
				class="btn-secondary px-4 py-3 text-left"
				aria-expanded={showVerify}
				onclick={() => (showVerify = !showVerify)}
			>
				{m.verify_profile_button({}, options)}
			</button>
			{#if showVerify}
				<VerifyCodeForm
					initialCode={storeNavigation.activeVerifyCode ?? ""}
					autoSubmit={Boolean(storeNavigation.activeVerifyCode)}
					onVerified={() => {
						showVerify = false;
						storeNavigation.activeVerifyCode = null;
					}}
					onSkip={() => {
						showVerify = false;
						storeNavigation.activeVerifyCode = null;
					}}
				/>
			{/if}
		{/if}
	</div>
{/if}

<style>
	.profile-account {
		display: flex;
		flex-direction: column;
		gap: var(--space-3);
	}

	.email-status {
		font-size: var(--text-small);
		text-transform: uppercase;
	}
</style>
