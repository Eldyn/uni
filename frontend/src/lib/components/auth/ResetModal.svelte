<script lang="ts">
	import Modal from "$components/common/Modal.svelte";
	import ResetPasswordForm from "./ResetPasswordForm.svelte";
	import { storeNavigation } from "$stores/navigation.svelte";
	import { storeI18n } from "$stores/i18n.svelte";
	import * as m from "$lib/paraglide/messages.js";

	let { onResetSuccess }: { onResetSuccess?: () => void } = $props();

	function handleSuccess() {
		const callback = onResetSuccess;
		storeNavigation.closeResetModal();
		storeNavigation.activeResetToken = null;
		callback?.();
	}
</script>

<Modal
	open={storeNavigation.isResetModalOpen}
	onclose={() => storeNavigation.closeResetModal()}
	ariaLabel={m.reset_title({}, { locale: storeI18n.locale })}
	contentClass="reset-modal-container pixel-corners relative w-full max-w-md p-6 [--pc-border:var(--border)] [--pc-fill:var(--surface)]"
>
	<button
		type="button"
		class="close-button"
		onclick={() => storeNavigation.closeResetModal()}
		aria-label={m.settings_close({}, { locale: storeI18n.locale })}
		title={m.settings_close({}, { locale: storeI18n.locale })}
	>
		<i class="pia pixelart-icons-font-close"></i>
	</button>

	<ResetPasswordForm
		token={storeNavigation.activeResetToken ?? ""}
		onResetSuccess={handleSuccess}
	/>
</Modal>

<style>
	:global(.reset-modal-container) {
		position: relative;
	}

	.close-button {
		position: absolute;
		top: 12px;
		right: 12px;
		background: none;
		border: none;
		color: var(--text);
		opacity: 0.5;
		cursor: pointer;
		transition: opacity 0.2s;
		z-index: 10;
	}

	.close-button:hover {
		opacity: 1;
		color: var(--danger);
	}
</style>
