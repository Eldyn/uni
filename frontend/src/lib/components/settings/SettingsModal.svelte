<script lang="ts">
	import { storeNavigation } from "$stores/navigation.svelte";
	import { storeAudio } from "$stores/audio.svelte";
	import Slider from "$components/lobby/settings/Slider.svelte";

	const pct = (v: number) => `${Math.round(v * 100)}%`;
</script>

<div class="modal-overlay dither-4" role="dialog" aria-modal="true" aria-labelledby="settings-title">
	<div class="modal-content w-full max-w-lg">
		<header class="mb-4 flex items-center justify-between gap-3">
			<h1 id="settings-title" class="title-screen text-2xl">Settings</h1>
			<button
				class="btn pixel-corners px-3 py-2"
				title="Close"
				aria-label="Close"
				onclick={() => storeNavigation.closeSettings()}
			>
				<i class="hn pix hn-close text-lg leading-none"></i>
			</button>
		</header>

		<div class="pixel-corners flex flex-col gap-6 bg-surface p-5 [--pc-border:var(--border)]">
			<h2 class="font-tiny text-sm uppercase tracking-wide text-text-h">Audio</h2>

			<Slider
				id="music-volume"
				label="Music volume"
				value={Math.round(storeAudio.musicVolume * 100)}
				min={0}
				max={100}
				format={(v) => pct(v / 100)}
				oncommit={(v) => storeAudio.setMusicVolume(v / 100)}
			/>

			<Slider
				id="sfx-volume"
				label="Sound effects volume"
				value={Math.round(storeAudio.sfxVolume * 100)}
				min={0}
				max={100}
				format={(v) => pct(v / 100)}
				oncommit={(v) => storeAudio.setSfxVolume(v / 100)}
			/>
		</div>
	</div>
</div>
