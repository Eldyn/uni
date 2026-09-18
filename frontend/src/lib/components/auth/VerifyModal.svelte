<script lang="ts">
	import Modal from "$components/common/Modal.svelte";
	import VerifyCodeForm from "./VerifyCodeForm.svelte";
	import { storeNavigation } from "$stores/navigation.svelte";
	import { storeI18n } from "$stores/i18n.svelte";
	import * as m from "$lib/paraglide/messages.js";
</script>

<Modal
	open={storeNavigation.isVerifyModalOpen}
	onclose={() => storeNavigation.closeVerifyModal()}
	ariaLabel={m.verify_title({}, { locale: storeI18n.locale })}
	contentClass="verify-modal-container pixel-corners relative w-full max-w-md p-6 [--pc-border:var(--border)] [--pc-fill:var(--surface)]"
>
	<button
		type="button"
		class="close-button"
		onclick={() => storeNavigation.closeVerifyModal()}
		aria-label={m.settings_close({}, { locale: storeI18n.locale })}
		title={m.settings_close({}, { locale: storeI18n.locale })}
	>
		<i class="pia pixelart-icons-font-close"></i>
	</button>

	<VerifyCodeForm
		initialCode={storeNavigation.activeVerifyCode ?? ""}
		autoSubmit={Boolean(storeNavigation.activeVerifyCode)}
		onVerified={() => {
			storeNavigation.closeVerifyModal();
			storeNavigation.activeVerifyCode = null;
		}}
		onSkip={() => {
			storeNavigation.closeVerifyModal();
			storeNavigation.activeVerifyCode = null;
		}}
	/>
</Modal>

<style>
	:global(.verify-modal-container) {
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
