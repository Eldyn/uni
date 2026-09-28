<script lang="ts">
	import { storeAudio } from "$stores/audio.svelte";
	import { storeI18n } from "$stores/i18n.svelte";
	import { storeAuth } from "$stores/auth.svelte";
	import { storeGame } from "$stores/game.svelte";
	import { storeAnimation } from "$stores/animation.svelte";
	import { storeRenderSettings } from "$stores/renderSettings.svelte";
	import { ws, ClientAction } from "$stores/ws.svelte";
	import Divisor from "$components/common/Divisor.svelte";
	import Slider from "$components/lobby/settings/Slider.svelte";
	import Toggle from "$components/lobby/settings/Toggle.svelte";
	import * as m from "$lib/paraglide/messages.js";

	let {
		showCredits = false,
		showDangerZone = false
	}: { showCredits?: boolean; showDangerZone?: boolean } = $props();

	const pct = (v: number) => `${Math.round(v * 100)}%`;

	const localeDisplayNames: Record<string, string> = {
		en: "English",
		it: "Italiano",
		es: "Español",
		de: "Deutsch",
		ko: "한국어",
		zh: "中文",
		ja: "日本語",
		uk: "Українська",
		ru: "Русский"
	};

	const localeFlags: Record<string, string> = {
		en: "🇬🇧",
		it: "🇮🇹",
		es: "🇪🇸",
		de: "🇩🇪",
		ko: "🇰🇷",
		zh: "🇨🇳",
		ja: "🇯🇵",
		uk: "🇺🇦",
		ru: "🇷🇺"
	};

	const SOCIAL_LINKS = [
		{ label: "GitHub", href: "https://github.com/Eldyn/uni", img: "github_icon.png" },
		{ label: "Discord", href: "https://discord.gg/QYJvfWqG5e", img: "discord_icon.png" },
		{
			label: "Bluesky",
			href: "https://bsky.app/profile/did:plc:pnfiqgr56esaantendnklouz",
			img: "bluesky_icon.png"
		},
		{ label: "X", href: "https://x.com/theunigamee", img: "x_icon.png" },
		{ label: "YouTube", href: "https://youtube.com/@play-uni", img: "youtube_icon.png" },
		{
			label: "Instagram",
			href: "https://www.instagram.com/the.uni.game/",
			img: "instagram_icon.png"
		},
		{ label: "TikTok", href: "https://tiktok.com/@the.uni.game", img: "tiktok_icon.png" }
	];

	const inMatch = $derived(storeGame.state !== null);

	let privacyMode = $state(
		typeof localStorage !== "undefined" && localStorage.getItem("uni_privacy_mode") === "true"
	);

	function setPrivacyMode(v: boolean) {
		privacyMode = v;
		if (typeof localStorage !== "undefined") {
			localStorage.setItem("uni_privacy_mode", v ? "true" : "false");
		}
		ws.emit(ClientAction.UserUpdatePrivacy, { privacy_mode: v });
	}

	let logoutPending = $state(false);

	async function handleLogout() {
		if (logoutPending) return;
		logoutPending = true;
		try {
			await storeAuth.logout();
		} finally {
			logoutPending = false;
		}
	}
</script>

<div class="settings-sections">
	<section class="settings-block">
		<Divisor text={m.settings_audio_heading({}, { locale: storeI18n.locale })} />
		<div class="settings-grid">
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

	<section class="settings-block">
		<Divisor text={m.settings_animation_heading({}, { locale: storeI18n.locale })} />
		<div class="settings-grid">
			<Toggle
				label={m.settings_animation_enabled({}, { locale: storeI18n.locale })}
				checked={storeAnimation.enabled}
				oncommit={(v) => storeAnimation.setEnabled(v)}
			/>

			<Toggle
				label={m.settings_animation_always_upright_values({}, { locale: storeI18n.locale })}
				description={m.settings_animation_always_upright_values_description(
					{},
					{ locale: storeI18n.locale }
				)}
				checked={storeAnimation.alwaysUprightValues}
				oncommit={(v) => storeAnimation.setAlwaysUprightValues(v)}
			/>

			<div class="span-all">
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
		</div>
	</section>

	<section class="settings-block">
		<Divisor text={m.settings_graphics_heading({}, { locale: storeI18n.locale })} />
		<div class="settings-grid">
			<Toggle
				label={m.settings_graphics_draw_pile({}, { locale: storeI18n.locale })}
				description={m.settings_graphics_draw_pile_description({}, { locale: storeI18n.locale })}
				checked={storeRenderSettings.drawPileThickness === "full"}
				oncommit={(v) => storeRenderSettings.setDrawPileThickness(v ? "full" : "capped")}
			/>

			<Toggle
				label={m.settings_graphics_click_to_play({}, { locale: storeI18n.locale })}
				description={m.settings_graphics_click_to_play_description(
					{},
					{ locale: storeI18n.locale }
				)}
				checked={storeRenderSettings.clickToPlay}
				oncommit={(v) => storeRenderSettings.setClickToPlay(v)}
			/>

			<Toggle
				label={m.settings_graphics_sync_cursor({}, { locale: storeI18n.locale })}
				description={m.settings_graphics_sync_cursor_description({}, { locale: storeI18n.locale })}
				checked={storeRenderSettings.syncCursorOnClick}
				oncommit={(v) => storeRenderSettings.setSyncCursorOnClick(v)}
			/>

			<Toggle
				label={m.settings_graphics_autoscroll({}, { locale: storeI18n.locale })}
				description={m.settings_graphics_autoscroll_description({}, { locale: storeI18n.locale })}
				checked={storeRenderSettings.autoScrollOnEdgeCreep}
				oncommit={(v) => storeRenderSettings.setAutoScrollOnEdgeCreep(v)}
			/>

			<Toggle
				label={m.settings_graphics_mat_ripple({}, { locale: storeI18n.locale })}
				description={m.settings_graphics_mat_ripple_description({}, { locale: storeI18n.locale })}
				checked={storeRenderSettings.matRipple}
				oncommit={(v) => storeRenderSettings.setMatRipple(v)}
			/>

			<Toggle
				label={m.settings_graphics_ambient_dust({}, { locale: storeI18n.locale })}
				description={m.settings_graphics_ambient_dust_description({}, { locale: storeI18n.locale })}
				checked={storeRenderSettings.ambientDust}
				oncommit={(v) => storeRenderSettings.setAmbientDust(v)}
			/>

			<Toggle
				label={m.game_privacy_mode({}, { locale: storeI18n.locale })}
				description={m.game_privacy_mode_desc({}, { locale: storeI18n.locale })}
				checked={privacyMode}
				oncommit={setPrivacyMode}
			/>
		</div>
	</section>

	<section class="settings-block">
		<Divisor text={m.settings_language_heading({}, { locale: storeI18n.locale })} />
		<div class="locale-row">
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

	<section class="settings-block">
		<Divisor text={m.settings_account_heading({}, { locale: storeI18n.locale })} />
		<div class="account-row">
			<span class="account-name">{storeAuth.username}</span>
			<button class="btn-danger btn-sm logout-btn" disabled={logoutPending} onclick={handleLogout}>
				<i class="pia pixelart-icons-font-logout" aria-hidden="true"></i>
				{m.home_logout({}, { locale: storeI18n.locale })}
			</button>
		</div>
	</section>

	{#if showCredits}
		<section class="settings-block">
			<Divisor text={m.settings_credits_heading({}, { locale: storeI18n.locale })} />
			<nav class="social-row" aria-label="Social links">
				{#each SOCIAL_LINKS as link}
					<a
						href={link.href}
						target="_blank"
						rel="noopener noreferrer"
						class="social-icon"
						aria-label={link.label}
					>
						<img src="/assets/social/{link.img}" alt={link.label} width="32" height="32" />
					</a>
				{/each}
			</nav>
		</section>
	{/if}

	<!-- Leaving a lobby is a plain action available directly in the lobby's own
	     header/exit button now (see LobbyScreen), not a destructive one that
	     belongs here — only quitting an in-progress match still does. -->
	{#if showDangerZone && inMatch}
		<section class="settings-block settings-block--danger">
			<Divisor text={m.settings_danger_zone_heading({}, { locale: storeI18n.locale })} />
			<button
				class="btn-secondary self-start"
				style="--pc-border: var(--danger); color: var(--danger);"
				onclick={() => storeGame.returnToLobby()}
			>
				{m.settings_quit_match({}, { locale: storeI18n.locale })}
			</button>
		</section>
	{/if}
</div>

<style>
	.settings-sections {
		display: flex;
		flex-direction: column;
		gap: var(--space-6);
	}

	.settings-block {
		display: flex;
		flex-direction: column;
		gap: var(--space-3);
	}

	.settings-grid {
		display: grid;
		grid-template-columns: 1fr;
		gap: var(--space-3) var(--space-5);
	}

	@media (min-width: 640px) {
		.settings-grid {
			grid-template-columns: 1fr 1fr;
		}
	}

	.span-all {
		grid-column: 1 / -1;
	}

	.locale-row {
		display: flex;
		flex-wrap: wrap;
		gap: var(--space-3);
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
		/* Slightly desaturated, not fully grayscale: many flags share the same
		   tricolor shape, and a pure grayscale filter makes them impossible to
		   tell apart. 90% keeps the pixel-art restraint while the active locale
		   still pops to full colour. */
		clip-path: none !important;
		filter: grayscale(0.9);
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

	.account-row {
		display: flex;
		align-items: center;
		justify-content: space-between;
		gap: var(--space-4);
		flex-wrap: wrap;
	}

	.account-name {
		font-family: var(--tiny);
		font-size: 18px;
		line-height: 1;
		color: var(--text-h);
	}

	.logout-btn {
		display: inline-flex;
		align-items: center;
		gap: var(--space-2);
	}

	.social-row {
		display: flex;
		flex-wrap: wrap;
		align-items: center;
		gap: var(--space-4);
	}

	.social-icon {
		opacity: 0.55;
		image-rendering: pixelated;
		transition: opacity var(--duration-fast) var(--ease-standard);
	}
	.social-icon:hover {
		opacity: 1;
	}
</style>
