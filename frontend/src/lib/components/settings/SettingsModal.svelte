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
	contentClass="pixel-corners w-full max-w-lg p-6 [--pc-border:var(--border)]"
>
	<header class="mb-4 flex items-center justify-between gap-3">
		<h1 id="settings-title" class="title-screen text-2xl">
			{m.settings_title({}, { locale: storeI18n.locale })}
		</h1>
		<button
			class="btn pixel-corners px-3 py-2"
			title={m.settings_close({}, { locale: storeI18n.locale })}
			aria-label={m.settings_close({}, { locale: storeI18n.locale })}
			onclick={() => storeNavigation.closeSettings()}
		>
			<i class="hn pix hn-close text-lg leading-none"></i>
		</button>
	</header>

	<SettingsSections showCredits={false} showDangerZone={true} />
</Modal>
