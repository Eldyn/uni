<script lang="ts">
	import { storeNavigation } from "$stores/navigation.svelte";
	import { storeAuth } from "$stores/auth.svelte";
	import TextEffects from "./common/TextEffects.svelte";

	let logoutPending = $state(false);
	/** Measured height of the fixed bottom dock, so the hero above it can
	 *  center itself in the space that's actually free, not the raw viewport
	 *  (which the dock would otherwise overlap on short/tall screens). */
	let dockHeight = $state(0);

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

	interface HubTile {
		label: string;
		icon: string;
		accent: string;
		/** Absent = tile has no action (still shows its badge, if any). */
		action?: () => void;
		/** Small caption under the label, e.g. "Soon" or "Log In!". */
		badge?: string;
	}

	// This hub only ever renders once signed in (guest or full account, see
	// the template below), so Stats is the only tile that still branches on
	// auth state: a guest taps it to open the login modal, a full account
	// goes straight to their stats. Settings is local browser state, works
	// either way. Decks/Skins have no action either way, unbuilt.
	const HUB_TILES = $derived<HubTile[]>([
		{
			label: "Stats",
			icon: "hn-crown",
			accent: "text-blue-card",
			action: storeAuth.isLoggedIn
				? () => storeNavigation.goto("stats")
				: () => storeNavigation.gotoAuth("login"),
			badge: storeAuth.isLoggedIn ? undefined : "Log In!"
		},
		{ label: "Decks", icon: "hn-viewblocks", accent: "text-green-card", badge: "Soon" },
		{ label: "Skins", icon: "hn-credit-card", accent: "text-red-card", badge: "Soon" },
		{
			label: "Settings",
			icon: "hn-cog",
			accent: "text-accent",
			action: () => storeNavigation.goto("settings")
		}
	]);

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
	class="relative min-h-screen overflow-hidden bg-bg"
	style="-webkit-font-smoothing: none; -moz-osx-font-smoothing: grayscale; font-smooth: never;"
>
	<!-- Background art: y-position tuned to align the dark cutout with the logo -->
	<div
		class="fixed left-0 top-0 z-0 h-[100svh] w-full bg-cover"
		style="
        background-image: url('/assets/bg_main.png');
        background-position: center 62%;
	    image-rendering: pixelated;
	    image-rendering: crisp-edges;
        "
	></div>

	<!-- Dock gradient: fixed, always bottom-half of viewport, independent of content height -->
	<div
		class="dock-bg pointer-events-none fixed bottom-0 left-0 right-0 z-[5] max-lg:landscape:hidden"
	></div>

	<!-- Hero zone: logo + welcome-back line, always centered in the space
	     above the dock (not bottom-anchored, which left a growing empty gap
	     up top the taller the dock got, worst on narrow/tall screens). -->
	<div
		class="relative z-10 flex min-h-screen flex-col items-center justify-center px-4 max-lg:landscape:hidden"
		style="padding-bottom: {dockHeight}px; transform: translateY(3rem);"
	>
		<div class="relative flex items-center justify-center">
			<!-- Dither halo behind the logo: sized/centered on the text itself so
			     it floats along with it, instead of the old page-fixed radial cutout
			     baked into bg_main.png, which no longer tracks a centered logo. -->
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
				{storeAuth.isLoggedIn ? "Welcome back," : "Playing as"}
				<span class="text-accent">{storeAuth.username}</span>
				<button
					class="logout-inline uppercase text-text/35 transition-colors hover:text-danger"
					style="font-family: var(--pypx); font-weight: 800;"
					onclick={handleLogout}
					disabled={logoutPending}
					>{logoutPending ? "Logging out…" : storeAuth.isGuest ? "Log out" : "Logout"}</button
				>
			</p>
		{/if}
	</div>

	<!-- Bottom dock: actions + nav, fixed so its own height (which varies a
	     lot by auth state) never pushes the hero above around, only pads it. -->
	<div
		bind:clientHeight={dockHeight}
		class="dock fixed inset-x-0 bottom-0 z-10 w-full px-4 pb-6 pt-6 max-lg:landscape:hidden"
	>
		<div class="relative mx-auto flex w-full max-w-sm flex-col gap-3">
			{#if !storeAuth.isLoggedIn && !storeAuth.isGuest}
				<!-- Signed out entirely: login + guest CTA -->
				<button
					class="btn pixel-corners w-full py-5 text-xl tracking-wider"
					onclick={() => storeNavigation.gotoAuth("login")}
				>
					Login
				</button>
				<p
					class="text-center font-extrabold uppercase tracking-widest text-text/30"
					style="font-family: var(--pypx);"
				>
					- or -
				</p>
				<button
					class="btn pixel-corners w-full py-5 text-xl tracking-wider"
					disabled={storeAuth.isLoading}
					onclick={playAsGuest}
				>
					{storeAuth.isLoading ? "Connecting…" : "Play as Guest"}
				</button>
			{:else}
				<!-- Logged-in or guest: primary CTA -->
				<button
					class="btn pixel-corners w-full py-4 text-xl tracking-wider"
					onclick={() => storeNavigation.goto("lobbies")}
				>
					Browse Lobbies
				</button>

				<!-- Hub: 4-tile horizontal action bar -->
				<div class="grid grid-cols-4 gap-2">
					{#each HUB_TILES as tile}
						<button
							class="hub-tile pixel-bordered flex flex-col items-center gap-1 py-3 text-center
							       {tile.action ? '' : 'opacity-50'}"
							style="--pc-fill: var(--surface); --pc-border: var(--border);"
							aria-disabled={!tile.action}
							onclick={() => tile.action?.()}
							aria-label="{tile.label}{tile.badge ? `, ${tile.badge}` : ''}"
						>
							<i class="hn pix {tile.icon} text-xl {tile.accent}"></i>
							<span class="font-tiny text-xs leading-tight text-text-h">{tile.label}</span>
							{#if tile.badge}
								<span class="font-tiny text-[0.6rem] leading-none text-accent/60">{tile.badge}</span
								>
							{/if}
						</button>
					{/each}
				</div>
			{/if}

			<!-- Site links + social icons -->
			<footer class="flex flex-col items-center gap-2">
				<nav
					class="flex flex-wrap justify-center gap-x-3 gap-y-1 font-tiny text-xs text-text/50"
					aria-label="Site links"
				>
					<a href="/how-to-play.html" class="transition-colors hover:text-accent">How to Play</a>
					<a href="/faq.html" class="transition-colors hover:text-accent">FAQ</a>
					<a href="/about.html" class="transition-colors hover:text-accent">About</a>
					<a href="/changelog.html" class="transition-colors hover:text-accent">Changelog</a>
					<a href="/credits.html" class="transition-colors hover:text-accent">Credits</a>
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

	<!-- Mobile landscape: a phone rotated sideways has almost no vertical room,
	     so the centered-hero + bottom-dock composition above (built for a tall
	     screen) gets rebalanced rather than squeezed. The brand column keeps the
	     portrait reading order (halo + logo, then the welcome line, then the
	     primary CTA) so the screen still looks like itself; only the four hub
	     tiles move out to a rail on the right edge, where the width freed up by
	     the rotation actually exists. Site links and socials collapse into one
	     full-width bottom row. Only kicks in under `lg` — a desktop or tablet in
	     landscape already has the vertical space the portrait layout wants. -->
	<div class="relative z-10 hidden h-[100svh] w-full flex-col max-lg:landscape:flex">
		<div class="relative flex flex-1 items-center justify-center overflow-hidden px-4">
			<div class="flex max-w-sm flex-col items-center gap-2">
				<div class="relative flex items-center justify-center">
					<div
						class="dither-halo-landscape pointer-events-none absolute z-0 bg-contain bg-center bg-no-repeat"
						style="background-image: url('/assets/dither-radial.png');"
					></div>
					<TextEffects
						text="UNI!"
						effect="undulate"
						class="logo-text title-hero title-hero-landscape relative z-10"
						font="var(--heading)"
						amplitude={12}
						speed={1}
						frequency={0.15}
					/>
				</div>

				{#if storeAuth.isLoggedIn || storeAuth.isGuest}
					<p class="relative z-20 text-center font-tiny text-xs text-text/70">
						{storeAuth.isLoggedIn ? "Welcome back," : "Playing as"}
						<span class="text-accent">{storeAuth.username}</span>
						<button
							class="logout-inline uppercase text-text/35 transition-colors hover:text-danger"
							style="font-family: var(--pypx); font-weight: 800;"
							onclick={handleLogout}
							disabled={logoutPending}
							>{logoutPending ? "Logging out…" : storeAuth.isGuest ? "Log out" : "Logout"}</button
						>
					</p>
				{/if}

				{#if !storeAuth.isLoggedIn && !storeAuth.isGuest}
					<div class="relative z-20 flex w-64 flex-col gap-2">
						<button
							class="btn pixel-corners w-full py-2.5 tracking-wider"
							onclick={() => storeNavigation.gotoAuth("login")}
						>
							Login
						</button>
						<button
							class="btn pixel-corners w-full py-2.5 tracking-wider"
							disabled={storeAuth.isLoading}
							onclick={playAsGuest}
						>
							{storeAuth.isLoading ? "Connecting…" : "Play as Guest"}
						</button>
					</div>
				{:else}
					<button
						class="btn pixel-corners relative z-20 w-64 py-2.5 tracking-wider"
						onclick={() => storeNavigation.goto("lobbies")}
					>
						Browse Lobbies
					</button>
				{/if}
			</div>

			<!-- Hub rail: vertically centred against the hero area only, so it
			     never reaches the chat launcher pinned to the bottom-right. -->
			{#if storeAuth.isLoggedIn || storeAuth.isGuest}
				<div class="absolute right-3 top-1/2 z-20 flex -translate-y-1/2 flex-col gap-1.5">
					{#each HUB_TILES as tile}
						<button
							class="hub-tile pixel-bordered flex w-24 items-center gap-2 px-2 py-1.5 text-left
							       {tile.action ? '' : 'opacity-50'}"
							style="--pc-fill: var(--surface); --pc-border: var(--border);"
							aria-disabled={!tile.action}
							onclick={() => tile.action?.()}
							aria-label="{tile.label}{tile.badge ? `, ${tile.badge}` : ''}"
						>
							<i class="hn pix {tile.icon} shrink-0 text-sm {tile.accent}"></i>
							<span class="font-tiny text-[0.6rem] leading-tight text-text-h">{tile.label}</span>
						</button>
					{/each}
				</div>
			{/if}
		</div>

		<!-- Bottom row: site links and socials on one line across the full width.
		     Right padding keeps the last social icon clear of the chat launcher. -->
		<footer
			class="flex w-full flex-wrap items-center justify-center gap-x-3 gap-y-1 px-4 pb-1.5 pr-16"
		>
			<nav
				class="flex flex-wrap items-center gap-x-3 gap-y-1 font-tiny text-[0.7rem] text-text/50"
				aria-label="Site links"
			>
				<a href="/how-to-play.html" class="transition-colors hover:text-accent">How to Play</a>
				<a href="/faq.html" class="transition-colors hover:text-accent">FAQ</a>
				<a href="/about.html" class="transition-colors hover:text-accent">About</a>
				<a href="/changelog.html" class="transition-colors hover:text-accent">Changelog</a>
				<a href="/credits.html" class="transition-colors hover:text-accent">Credits</a>
			</nav>
			<nav class="flex items-center gap-2.5" aria-label="Social links">
				{#each SOCIAL_LINKS as link}
					<a
						href={link.href}
						target="_blank"
						rel="noopener noreferrer"
						class="social-icon"
						aria-label={link.label}
					>
						<img src="/assets/social/{link.img}" alt={link.label} width="22" height="22" />
					</a>
				{/each}
			</nav>
		</footer>
	</div>
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

	/* Mobile landscape keeps .title-hero (and its white fill) and only retunes
	   the size, scaling off height — the axis that's actually scarce on a
	   rotated phone — rather than the portrait rule's viewport width. */
	:global(.logo-text.title-hero.title-hero-landscape) {
		font-size: clamp(2.25rem, 17vh, 4.5rem);
	}

	/* Same aura as the portrait halo, sized against the short axis so it stays
	   proportional to the smaller landscape logo instead of flooding the row. */
	.dither-halo-landscape {
		width: clamp(14rem, 62vh, 26rem);
		height: clamp(14rem, 62vh, 26rem);
		image-rendering: pixelated;
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

	.hub-tile:not([aria-disabled="true"]):hover {
		--pc-border: var(--accent);
		cursor: pointer;
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

	.social-icon {
		opacity: 0.45;
		transition: opacity 0.15s;
		image-rendering: pixelated;
	}
	.social-icon:hover {
		opacity: 1;
	}
</style>
