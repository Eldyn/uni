<script lang="ts">
	import { storeNavigation } from "$stores/navigation.svelte";
	import { storeAudio } from "$stores/audio.svelte";
	import Slider from "$components/lobby/settings/Slider.svelte";

	const pct = (v: number) => `${Math.round(v * 100)}%`;
</script>

<div
	class="fixed inset-0 flex flex-col overflow-y-auto bg-cover bg-center"
	style="
        background-image: url('/assets/bg_main.png');
        background-position: center 62%;
	    image-rendering: pixelated;
	    image-rendering: crisp-edges;
        "
>
	<header
		class="flex items-center justify-between gap-3 border-b-2 border-border bg-bg px-4 py-2 sm:px-6 sm:py-3 lg:px-10"
	>
		<h1 class="title-screen text-2xl sm:text-3xl lg:text-4xl">Settings</h1>
		<button
			class="btn pixel-corners px-3 py-2 sm:px-5 sm:py-3"
			title="Back"
			aria-label="Back"
			onclick={() => storeNavigation.goto("main")}
		>
			<i class="hn pix hn-arrow-left text-lg leading-none sm:hidden"></i>
			<span class="hidden text-base uppercase sm:inline">Back</span>
		</button>
	</header>

	<div class="mx-auto w-full max-w-lg flex-1 px-4 py-6 sm:px-6">
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
