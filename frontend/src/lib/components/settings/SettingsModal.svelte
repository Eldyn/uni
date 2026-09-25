<script lang="ts">
	import Modal from "$components/common/Modal.svelte";
	import SettingsSections from "./SettingsSections.svelte";
	import { storeNavigation } from "$stores/navigation.svelte";
	import { storeI18n } from "$stores/i18n.svelte";
	import * as m from "$lib/paraglide/messages.js";
</script>

<Modal
	open={storeNavigation.isSettingsOpen}
	onclose={() => storeNavigation.closeSettings()}
	titleId="settings-title"
	contentClass="pixel-corners flex max-h-[85vh] w-full max-w-3xl flex-col p-5 sm:p-7 [--pc-border:var(--border)]"
>
	<header class="mb-4 flex shrink-0 items-center justify-between gap-3">
		<h1 id="settings-title" class="title-screen text-2xl">
			{m.settings_title({}, { locale: storeI18n.locale })}
		</h1>
		<button
			class="btn pixel-corners px-3 py-2"
			title={m.settings_close({}, { locale: storeI18n.locale })}
			aria-label={m.settings_close({}, { locale: storeI18n.locale })}
			onclick={() => storeNavigation.closeSettings()}
		>
			<i class="pia pixelart-icons-font-close text-lg leading-none"></i>
		</button>
	</header>

	<div class="flex-1 overflow-y-auto pr-1 pb-1 scrollbar-accent">
		<SettingsSections showCredits={false} showDangerZone={true} />
	</div>
</Modal>
