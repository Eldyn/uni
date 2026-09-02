<script lang="ts">
	import { storeNavigation } from "$stores/navigation.svelte";
	import { storeAudio } from "$stores/audio.svelte";
	import { storeI18n } from "$stores/i18n.svelte";
	import Slider from "$components/lobby/settings/Slider.svelte";
	import * as m from "$lib/paraglide/messages.js";

	const pct = (v: number) => `${Math.round(v * 100)}%`;
</script>

<div class="modal-overlay dither-4" role="dialog" aria-modal="true" aria-labelledby="settings-title">
	<div class="modal-content w-full max-w-lg">
		<header class="mb-4 flex items-center justify-between gap-3">
			<h1 id="settings-title" class="title-screen text-2xl">{m.settings_title()}</h1>
			<button
				class="btn pixel-corners px-3 py-2"
				title={m.settings_close()}
				aria-label={m.settings_close()}
				onclick={() => storeNavigation.closeSettings()}
			>
				<i class="hn pix hn-close text-lg leading-none"></i>
			</button>
		</header>

		<div class="pixel-corners flex flex-col gap-6 bg-surface p-5 [--pc-border:var(--border)]">
			<h2 class="font-tiny text-sm uppercase tracking-wide text-text-h">
				{m.settings_audio_heading()}
			</h2>

			<Slider
				id="music-volume"
				label={m.settings_music_volume()}
				value={Math.round(storeAudio.musicVolume * 100)}
				min={0}
				max={100}
				format={(v) => pct(v / 100)}
				oncommit={(v) => storeAudio.setMusicVolume(v / 100)}
			/>

			<Slider
				id="sfx-volume"
				label={m.settings_sfx_volume()}
				value={Math.round(storeAudio.sfxVolume * 100)}
				min={0}
				max={100}
				format={(v) => pct(v / 100)}
				oncommit={(v) => storeAudio.setSfxVolume(v / 100)}
			/>
		</div>

		<div class="pixel-corners mt-6 flex flex-col gap-3 bg-surface p-5 [--pc-border:var(--border)]">
			<h2 class="font-tiny text-sm uppercase tracking-wide text-text-h">
				{m.settings_language_heading()}
			</h2>
			<div class="flex gap-2">
				<button
					class="btn-secondary pixel-corners px-4 py-2"
					aria-pressed={storeI18n.locale === "en"}
					onclick={() => storeI18n.setLocale("en")}
				>
					English
				</button>
				<button
					class="btn-secondary pixel-corners px-4 py-2"
					aria-pressed={storeI18n.locale === "it"}
					onclick={() => storeI18n.setLocale("it")}
				>
					Italiano
				</button>
			</div>
		</div>
	</div>
</div>
