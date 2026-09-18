<script lang="ts">
	import Modal from "$lib/components/common/Modal.svelte";
	import { storeI18n } from "$stores/i18n.svelte";
	import * as m from "$lib/paraglide/messages.js";

	let {
		open,
		onConfirm,
		onCancel
	}: { open: boolean; onConfirm: () => void; onCancel: () => void } = $props();
</script>

<Modal
	{open}
	dismissible={true}
	onclose={onCancel}
	titleId="exit-confirm-title"
	contentClass="exit-confirm-content pixel-corners"
>
	<h2 id="exit-confirm-title">{m.game_exit_confirm_title({}, { locale: storeI18n.locale })}</h2>
	<p>{m.game_exit_confirm_body({}, { locale: storeI18n.locale })}</p>
	<div class="exit-confirm-actions">
		<button type="button" class="btn pixel-corners" onclick={onCancel}>
			{m.game_exit_confirm_no({}, { locale: storeI18n.locale })}
		</button>
		<button type="button" class="btn pixel-corners btn-danger" onclick={onConfirm}>
			{m.game_exit_confirm_yes({}, { locale: storeI18n.locale })}
		</button>
	</div>
</Modal>

<style>
	.exit-confirm-actions {
		display: flex;
		gap: 12px;
		justify-content: center;
		margin-top: 12px;
	}

	.btn-danger {
		background: var(--danger);
		color: #fff;
	}
</style>
