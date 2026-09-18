<script lang="ts">
	import Modal from "$components/common/Modal.svelte";
	import LoginForm from "./LoginForm.svelte";
	import RegisterForm from "./RegisterForm.svelte";
	import { storeNavigation } from "$stores/navigation.svelte";
	import { storeI18n } from "$stores/i18n.svelte";
	import * as m from "$lib/paraglide/messages.js";

	let {
		onAuthSuccess,
		initialTab = "login"
	}: { onAuthSuccess: () => void; initialTab?: "login" | "register" } = $props();

	let activeTab = $state<"login" | "register">(initialTab);
</script>

<Modal
	bind:open={storeNavigation.isAuthModalOpen}
	ariaLabel={m.auth_modal_aria({}, { locale: storeI18n.locale })}
	contentClass="auth-container pixel-corners w-full"
>
	<button
		type="button"
		class="close-button"
		onclick={() => storeNavigation.closeAuthModal()}
		aria-label={m.settings_close({}, { locale: storeI18n.locale })}
		title={m.settings_close({}, { locale: storeI18n.locale })}
	>
		<i class="pia pixelart-icons-font-close"></i>
	</button>

	<div class="auth-tabs">
		<button
			type="button"
			class="tab-button"
			class:active={activeTab === "login"}
			onclick={() => (activeTab = "login")}
		>
			{m.auth_login_tab({}, { locale: storeI18n.locale })}
		</button>
		<button
			type="button"
			class="tab-button"
			class:active={activeTab === "register"}
			onclick={() => (activeTab = "register")}
		>
			{m.auth_register_tab({}, { locale: storeI18n.locale })}
		</button>
	</div>

	<div class="auth-content">
		{#if activeTab === "login"}
			<LoginForm onLoginSuccess={onAuthSuccess} />
		{:else}
			<RegisterForm
				onRegisterSuccess={() => {
					onAuthSuccess();
				}}
			/>
		{/if}
	</div>
</Modal>

<style>
	:global(.auth-container) {
		position: relative;
		font-family: "Pixel";
	}

	.close-button {
		position: absolute;
		top: 8px;
		right: 8px;
		background: none;
		border: none;
		color: var(--text);
		opacity: 0.5;
		cursor: pointer;
		transition: opacity 0.2s;
	}

	.close-button:hover {
		opacity: 1;
		color: var(--danger);
	}

	.auth-tabs {
		display: flex;
		gap: 0;
		margin-bottom: 24px;
		border-bottom: 1px solid var(--border);
	}

	.tab-button {
		flex: 1;
		padding: 12px;
		background: none;
		border: none;
		font-size: 16px;
		font-family: "Pixel";
		font-weight: 500;
		letter-spacing: 1px;
		color: var(--text);
		cursor: pointer;
		border-bottom: 2px solid transparent;
		transition: all 0.2s;
	}

	.tab-button:hover {
		color: var(--accent);
	}

	.tab-button.active {
		color: var(--accent);
		border-bottom-color: var(--accent);
	}

	.auth-content {
		animation: fadeIn 0.3s ease-out;
	}

	@keyframes fadeIn {
		from {
			opacity: 0;
		}
		to {
			opacity: 1;
		}
	}

	@media (max-width: 1024px) {
		:global(.auth-container) {
			padding: 24px;
		}
	}
</style>
