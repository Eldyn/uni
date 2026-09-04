<script lang="ts">
	import { storeNavigation } from "$stores/navigation.svelte";
	import { storeAuth } from "$stores/auth.svelte";
	import { storeI18n } from "$stores/i18n.svelte";
	import * as m from "$lib/paraglide/messages.js";
	import TextEffects from "./common/TextEffects.svelte";
	import HomeScreen from "$components/home/HomeScreen.svelte";
	import LandingContent from "$components/landing/LandingContent.svelte";

	let logoutPending = $state(false);

	async function playAsGuest() {
		// INFO: No forced navigation, playing as guest from Main leaves you on
		//       Main (now showing the guest hub) instead of jumping to Lobbies.
		await storeAuth.loginAsGuest();
	}

	async function handleLogout() {
		if (logoutPending) return;
		logoutPending = true;
		try {
			await storeAuth.logout();
		} finally {
			logoutPending = false;
		}
	}

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
</script>

<div
	class="main-screen-root relative min-h-full overflow-y-auto bg-bg"
	style="-webkit-font-smoothing: none; -moz-osx-font-smoothing: grayscale; font-smooth: never;"
>
	<!-- Hero zone: pinned to roughly one viewport tall so the background art,
	     logo and bottom dock stay anchored near the top of the page instead of
	     chasing the user down the scrollable marketing content below (that
	     content flows as a normal sibling, outside this box). -->
	<div class="hero-zone relative min-h-screen">
		<!-- Background art: y-position tuned to align the dark cutout with the logo.
		     Absolute (scoped to the hero zone), not fixed to the viewport —
		     MainScreen now lives inside ShellFrame's .shell-content alongside the
		     TopBar/NavBar chrome, which a fixed layer would paint over. -->
		<div
			class="absolute inset-0 z-0 bg-cover"
			style="
        background-image: url('/assets/bg_main.png');
        background-position: center 62%;
	    image-rendering: pixelated;
	    image-rendering: crisp-edges;
        "
		></div>

		<!-- Dock gradient: absolute, always bottom-half of the hero zone, independent of content height -->
		<div class="dock-bg pointer-events-none absolute bottom-0 left-0 right-0 z-[5]"></div>

		<!-- Hero: logo + welcome-back line, always centered in the space
		     above the dock (not bottom-anchored, which left a growing empty gap
		     up top the taller the dock got, worst on narrow/tall screens). -->
		<div
			class="landing-grid relative z-10 flex min-h-full flex-col px-4"
		>
			<div class="landing-title flex flex-col items-center justify-center pt-8">
				<div class="relative flex items-center justify-center">
					<div
						class="dither-halo pointer-events-none absolute z-0 bg-contain bg-center bg-no-repeat"
						style="background-image: url('/assets/dither-radial.png');"
					></div>
					<TextEffects
						text="UNI!"
						effect="undulate"
						class="logo-text title-hero relative z-10"
						font="var(--heading)"
						amplitude={20}
						speed={1}
						frequency={0.15}
					/>
				</div>
				{#if storeAuth.isLoggedIn || storeAuth.isGuest}
					<p class="relative z-20 mt-4 text-center font-tiny text-sm text-text/70">
						{storeAuth.isLoggedIn
							? m.home_welcome_back({}, { locale: storeI18n.locale })
							: m.home_playing_as({}, { locale: storeI18n.locale })}
						<TextEffects
							text={storeAuth.username}
							effect="undulate"
							class="font-tiny text-accent"
							amplitude={6}
							speed={2}
							frequency={0.15}
						/>
						<button
							class="logout-inline uppercase text-text/35 transition-colors hover:text-danger"
							style="font-family: var(--pypx); font-weight: 800;"
							onclick={handleLogout}
							disabled={logoutPending}
							>{logoutPending
								? m.home_logging_out({}, { locale: storeI18n.locale })
								: storeAuth.isGuest
									? m.home_log_out_guest({}, { locale: storeI18n.locale })
									: m.home_logout({}, { locale: storeI18n.locale })}</button
						>
					</p>
				{/if}
			</div>

			<div class="landing-spacer" aria-hidden="true"></div>

			<div class="landing-actions mx-auto flex w-full max-w-sm flex-col gap-3">
				{#if !storeAuth.isLoggedIn && !storeAuth.isGuest}
					<button
						class="btn pixel-corners w-full py-5 text-xl tracking-wider"
						onclick={() => storeNavigation.gotoAuth("login")}
					>
						{m.home_login({}, { locale: storeI18n.locale })}
					</button>
					<p
						class="text-center font-extrabold uppercase tracking-widest text-text/30"
						style="font-family: var(--pypx);"
					>
						{m.home_or_separator({}, { locale: storeI18n.locale })}
					</p>
					<button
						class="btn pixel-corners w-full py-5 text-xl tracking-wider"
						disabled={storeAuth.isLoading}
						onclick={playAsGuest}
					>
						{storeAuth.isLoading
							? m.home_connecting({}, { locale: storeI18n.locale })
							: m.home_play_as_guest({}, { locale: storeI18n.locale })}
					</button>
				{:else}
					<HomeScreen />
				{/if}
			</div>

			<div class="landing-spacer" aria-hidden="true"></div>

			<footer class="landing-footer flex flex-col items-center gap-2 pb-6">
				<nav
					class="flex flex-wrap justify-center gap-x-3 gap-y-1 font-tiny text-xs text-text/50"
					aria-label="Site links"
				>
					<a href="/how-to-play.html" class="transition-colors hover:text-accent"
						>{m.home_footer_how_to_play({}, { locale: storeI18n.locale })}</a
					>
					<a href="/faq.html" class="transition-colors hover:text-accent"
						>{m.home_footer_faq({}, { locale: storeI18n.locale })}</a
					>
					<a href="/about.html" class="transition-colors hover:text-accent"
						>{m.home_footer_about({}, { locale: storeI18n.locale })}</a
					>
					<a href="/changelog.html" class="transition-colors hover:text-accent"
						>{m.home_footer_changelog({}, { locale: storeI18n.locale })}</a
					>
					<a href="/credits.html" class="transition-colors hover:text-accent"
						>{m.home_footer_credits({}, { locale: storeI18n.locale })}</a
					>
				</nav>
				<nav class="flex items-center gap-3" aria-label="Social links">
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
			</footer>
		</div>
	</div>

	{#if !storeAuth.isLoggedIn && !storeAuth.isGuest}
		<a href="#landing-content" class="skip-link">More about UNI!</a>
		<div id="landing-content" tabindex="-1">
			<LandingContent />
		</div>
	{/if}
</div>

<style>
	:global(.logo-text) {
		gap: 0.4rem;
	}

	/* Logo scales with viewport width on narrow screens (portrait mobile),
	   stays at 10rem on wide/desktop. clamp handles it without a hard breakpoint. */
	:global(.logo-text.title-hero) {
		font-size: clamp(4rem, 26vw, 10rem);
	}

	/* Square halo, generously oversized relative to the logo text so it reads
	   as an aura rather than a tight outline; scales with the same viewport
	   range as the logo itself so it keeps looking proportional at every size. */
	.dither-halo {
		width: clamp(27rem, 135vw, 51rem);
		height: clamp(27rem, 135vw, 51rem);
		image-rendering: pixelated;
	}

	.dock-bg {
		height: 50vh;
		background-image: repeating-conic-gradient(rgba(16, 17, 22, 0.97) 0% 25%, transparent 0% 50%);
		background-size: 4px 4px;
		-webkit-mask-image: linear-gradient(to top, black 35%, transparent 100%);
		mask-image: linear-gradient(to top, black 35%, transparent 100%);
	}

	.logout-inline {
		display: inline;
		background: transparent;
		border: none;
		padding: 0;
		cursor: pointer;
		clip-path: none !important;
		border-radius: 0 !important;
		font-size: inherit;
	}
	.logout-inline:hover {
		text-decoration: underline;
		text-decoration-thickness: 2px;
	}
	.logout-inline:disabled {
		opacity: 0.4;
		cursor: not-allowed;
	}

	.skip-link {
		position: absolute;
		left: -9999px;
		top: 0;
		z-index: 30;
		padding: var(--space-2) var(--space-4);
		background: var(--accent);
		color: #fff;
	}
	.skip-link:focus {
		left: var(--space-4);
		top: var(--space-4);
	}

	.social-icon {
		opacity: 0.45;
		transition: opacity 0.15s;
		image-rendering: pixelated;
	}
	.social-icon:hover {
		opacity: 1;
	}

	/* Scroll-driven reveal: the logo/hero shrinks and the dock's actions
	   dock to the bottom as the user scrolls into the marketing content
	   below. Only opacity/transform animate; that is non-negotiable, since
	   anything gated behind
	   display/visibility is discounted by search engines, defeating the
	   entire point of this content existing). Every rule below defaults to
	   its normal, unanimated appearance; the animation only kicks in inside
	   the @supports block, so browsers without animation-timeline still get
	   a fully visible, fully sized, statically positioned page. */
	/* The named scroll timeline lives on the element that actually scrolls —
	   MainScreen's own root, now that it's the scroll container (see the
	   overflow-y-auto above) — not on html, which never scrolls here. */
	.main-screen-root {
		scroll-timeline: --landing-scroll block;
	}

	.landing-grid {
		display: grid;
		grid-template-rows: auto 1fr auto 1fr auto;
		min-height: 100svh;
	}

	.landing-spacer {
		min-height: 0;
	}

	@supports (animation-timeline: scroll()) {
		.landing-grid {
			animation: landing-hero-shrink linear both;
			animation-timeline: --landing-scroll;
			animation-range: 0 60vh;
		}

		:global(.landing-content) {
			animation: landing-reveal linear both;
			animation-timeline: --landing-scroll;
			animation-range: 20vh 80vh;
		}
	}

	@keyframes landing-hero-shrink {
		from {
			transform: scale(1);
		}
		to {
			transform: scale(0.92);
		}
	}

	@keyframes landing-reveal {
		from {
			opacity: 0;
			transform: translateY(24px);
		}
		to {
			opacity: 1;
			transform: translateY(0);
		}
	}

	@media (prefers-reduced-motion: reduce) {
		.landing-grid {
			animation: none;
			transform: none;
		}
		:global(.landing-content) {
			animation: none;
			opacity: 1;
			transform: none;
		}
	}
</style>
