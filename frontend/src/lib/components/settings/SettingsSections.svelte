<script lang="ts">
	import { storeAudio } from "$stores/audio.svelte";
	import { storeI18n } from "$stores/i18n.svelte";
	import { storeAuth } from "$stores/auth.svelte";
	import { storeGame } from "$stores/game.svelte";
	import { storeAnimation } from "$stores/animation.svelte";
	import { storeRenderSettings } from "$stores/renderSettings.svelte";
	import Slider from "$components/lobby/settings/Slider.svelte";
	import Toggle from "$components/lobby/settings/Toggle.svelte";
	import EnumSelector from "$components/lobby/settings/EnumSelector.svelte";
	import * as m from "$lib/paraglide/messages.js";

	let { showCredits = false, showDangerZone = false }: { showCredits?: boolean; showDangerZone?: boolean } =
		$props();

	const pct = (v: number) => `${Math.round(v * 100)}%`;

	const localeDisplayNames: Record<string, string> = {
		en: "English",
		it: "Italiano",
		es: "Español",
		de: "Deutsch",
		ko: "한국어",
		zh: "中文",
		ja: "日本語"
	};

	const localeFlags: Record<string, string> = {
		en: "🇬🇧",
		it: "🇮🇹",
		es: "🇪🇸",
		de: "🇩🇪",
		ko: "🇰🇷",
		zh: "🇨🇳",
		ja: "🇯🇵"
	};

	const SOCIAL_LINKS = [
		{ label: "GitHub", href: "https://github.com/Eldyn/uni" },
		{ label: "Discord", href: "https://discord.gg/QYJvfWqG5e" },
		{ label: "Bluesky", href: "https://bsky.app/profile/did:plc:pnfiqgr56esaantendnklouz" },
		{ label: "X", href: "https://x.com/theunigamee" },
		{ label: "YouTube", href: "https://youtube.com/@play-uni" },
		{ label: "Instagram", href: "https://www.instagram.com/the.uni.game/" },
		{ label: "TikTok", href: "https://tiktok.com/@the.uni.game" }
	];

	const inMatch = $derived(storeGame.state !== null);
</script>

{#snippet sectionHeading(text: string)}
	<h2 class="font-tiny text-sm uppercase tracking-wide text-text-h">{text}</h2>
{/snippet}

<div class="settings-sections flex flex-col gap-6">
	<section class="panel settings-panel">
		{@render sectionHeading(m.settings_audio_heading({}, { locale: storeI18n.locale }))}
		<div class="mt-4 flex flex-col gap-4">
			<Slider
				id="music-volume"
				label={m.settings_music_volume({}, { locale: storeI18n.locale })}
				value={Math.round(storeAudio.musicVolume * 100)}
				min={0}
				max={100}
				live={true}
				format={(v) => pct(v / 100)}
				oncommit={(v) => storeAudio.setMusicVolume(v / 100)}
			/>

			<Slider
				id="sfx-volume"
				label={m.settings_sfx_volume({}, { locale: storeI18n.locale })}
				value={Math.round(storeAudio.sfxVolume * 100)}
				min={0}
				max={100}
				live={true}
				format={(v) => pct(v / 100)}
				oncommit={(v) => storeAudio.setSfxVolume(v / 100)}
			/>
		</div>
	</section>

	<section class="panel settings-panel">
		{@render sectionHeading(m.settings_animation_heading({}, { locale: storeI18n.locale }))}
		<div class="mt-4 flex flex-col gap-4">
			<Toggle
				label={m.settings_animation_enabled({}, { locale: storeI18n.locale })}
				checked={storeAnimation.enabled}
				oncommit={(v) => storeAnimation.setEnabled(v)}
			/>

			<Toggle
				label={m.settings_animation_always_upright_values({}, { locale: storeI18n.locale })}
				description={m.settings_animation_always_upright_values_description({}, { locale: storeI18n.locale })}
				checked={storeAnimation.alwaysUprightValues}
				oncommit={(v) => storeAnimation.setAlwaysUprightValues(v)}
			/>

			<Slider
				id="animation-speed"
				label={m.settings_animation_speed({}, { locale: storeI18n.locale })}
				value={Math.round(storeAnimation.speedMultiplier * 100)}
				min={50}
				max={300}
				disabled={!storeAnimation.enabled}
				live={false}
				format={(v) => `${(v / 100).toFixed(2)}x`}
				oncommit={(v) => storeAnimation.setSpeedMultiplier(v / 100)}
			/>
		</div>
	</section>

	<section class="panel settings-panel">
		{@render sectionHeading("Graphics & Gameplay")}
		<div class="mt-4 flex flex-col gap-4">
			<Toggle
				label="Full 3D draw pile stack"
				description="Render complete 3D card stack for the draw pile instead of capped depth."
				checked={storeRenderSettings.drawPileThickness === "full"}
				oncommit={(v) => storeRenderSettings.setDrawPileThickness(v ? "full" : "capped")}
			/>

			<Toggle
				label="Click to play card"
				description="Play card immediately with a single click or Enter press instead of two-step confirm."
				checked={storeRenderSettings.clickToPlay}
				oncommit={(v) => storeRenderSettings.setClickToPlay(v)}
			/>

			<Toggle
				label="Sync cursor on click"
				description="Keep keyboard navigation cursor synced with mouse-clicked cards."
				checked={storeRenderSettings.syncCursorOnClick}
				oncommit={(v) => storeRenderSettings.setSyncCursorOnClick(v)}
			/>

			<Toggle
				label="Auto-scroll on edge creep"
				description="Automatically scroll hand cards when pointer or cursor hovers near edges."
				checked={storeRenderSettings.autoScrollOnEdgeCreep}
				oncommit={(v) => storeRenderSettings.setAutoScrollOnEdgeCreep(v)}
			/>

			<EnumSelector
				label="Seat name arc"
				description="Curve opponent player names along an arc instead of flat text."
				value={storeRenderSettings.seatNameArcMode}
				options={[
					{ value: "overhead", label: "Overhead", description: "Curved over avatar head" },
					{ value: "cards-outer", label: "Outer cards", description: "Curved along outer card tips" },
					{ value: "cards-inner", label: "Inner cards", description: "Curved between avatar and cards" }
				]}
				oncommit={(v) => storeRenderSettings.setSeatNameArcMode(v)}
			/>
		</div>
	</section>

	<section class="panel settings-panel">
		{@render sectionHeading(m.settings_language_heading({}, { locale: storeI18n.locale }))}
		<div class="mt-4 flex flex-wrap gap-3">
			{#each storeI18n.locales as locale (locale)}
				<button
					class="locale-flag-btn"
					class:active={storeI18n.locale === locale}
					aria-pressed={storeI18n.locale === locale}
					title={localeDisplayNames[locale] ?? locale}
					aria-label={localeDisplayNames[locale] ?? locale}
					onclick={() => storeI18n.setLocale(locale)}
				>
					{localeFlags[locale] ?? locale}
				</button>
			{/each}
		</div>
	</section>

	<section class="panel settings-panel">
		{@render sectionHeading(m.settings_account_heading({}, { locale: storeI18n.locale }))}
		<p class="mt-4 font-tiny text-base text-text-h">{storeAuth.username}</p>
	</section>

	{#if showCredits}
		<section class="panel settings-panel">
			{@render sectionHeading(m.settings_credits_heading({}, { locale: storeI18n.locale }))}
			<nav class="mt-4 flex flex-wrap gap-3 font-tiny text-sm text-text/70" aria-label="Credits links">
				{#each SOCIAL_LINKS as link}
					<a href={link.href} target="_blank" rel="noopener noreferrer" class="transition-colors hover:text-accent">
						{link.label}
					</a>
				{/each}
			</nav>
		</section>
	{/if}

	<!-- Leaving a lobby is a plain action available directly in the lobby's own
	     header/exit button now (see LobbyScreen), not a destructive one that
	     belongs here — only quitting an in-progress match still does. -->
	{#if showDangerZone && inMatch}
		<section class="panel settings-panel settings-panel--danger">
			{@render sectionHeading(m.settings_danger_zone_heading({}, { locale: storeI18n.locale }))}
			<div class="mt-4 flex flex-col gap-2">
				<button class="btn-secondary px-4 py-2" style="--pc-border: var(--danger); color: var(--danger);" onclick={() => storeGame.returnToLobby()}>
					{m.settings_quit_match({}, { locale: storeI18n.locale })}
				</button>
			</div>
		</section>
	{/if}
</div>

<style>
	.settings-panel {
		box-shadow: var(--elevation-1);
		padding: var(--space-6);
	}
	.settings-panel--danger {
		border-color: var(--danger);
	}

	.locale-flag-btn {
		background: transparent;
		border: none;
		padding: 0;
		font-size: 2rem;
		line-height: 1;
		cursor: pointer;
		/* No border/fill to notch here (plain emoji glyph) — override the
		   global button pixel-notch reset, which would otherwise nibble the
		   flag's corners for no visual reason. */
		clip-path: none !important;
		filter: grayscale(1);
		opacity: 0.6;
		transition:
			filter var(--duration-fast) var(--ease-standard),
			opacity var(--duration-fast) var(--ease-standard);
	}
	.locale-flag-btn:hover,
	.locale-flag-btn.active {
		filter: grayscale(0);
		opacity: 1;
	}
</style>
