<script lang="ts">
	import { storeNavigation } from "../stores/navigation.svelte";
	import { storeAuth } from "../stores/auth.svelte";
	import TextEffects from "./common/TextEffects.svelte";

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

	const HUB_TILES = [
		{
			label: "Stats",
			icon: "hn-crown",
			accent: "text-blue-card",
			available: true,
			action: () => storeNavigation.goto("stats")
		},
		{
			label: "Decks",
			icon: "hn-viewblocks",
			accent: "text-green-card",
			available: false,
			action: () => {}
		},
		{
			label: "Skins",
			icon: "hn-credit-card",
			accent: "text-red-card",
			available: false,
			action: () => {}
		},
		{
			label: "Settings",
			icon: "hn-cog",
			accent: "text-accent",
			available: true,
			action: () => storeNavigation.goto("settings")
		}
	] as const;
</script>

<div
	class="relative flex min-h-screen flex-col overflow-hidden bg-bg"
	style="-webkit-font-smoothing: none; -moz-osx-font-smoothing: grayscale; font-smooth: never;"
>
	<!-- Background art: y-position tuned to align the dark cutout with the logo -->
	<div
		class="fixed inset-0 z-0 bg-cover"
		style="background-image: url('/assets/bg_main.png'); background-position: center 62%;"
	></div>

	<!-- Dock gradient: fixed, always bottom-half of viewport, independent of content height -->
	<div class="dock-bg pointer-events-none fixed bottom-0 left-0 right-0 z-[5]"></div>

	<!-- Upper hero zone: logo pushed toward the dock, not mid-viewport -->
	<div class="relative z-10 flex flex-1 flex-col items-center justify-end px-4 pb-6">
		<TextEffects
			text="UNI!"
			effect="undulate"
			class="logo-text title-hero"
			font="var(--heading)"
			amplitude={20}
			speed={1}
			frequency={0.15}
		/>
		{#if storeAuth.isLoggedIn}
			<p class="mt-4 font-tiny text-sm text-text/70">
				Welcome back, <span class="text-accent">{storeAuth.username}</span>
				<button
					class="logout-inline uppercase text-text/35 transition-colors hover:text-danger"
					style="font-family: var(--pypx); font-weight: 800;"
					onclick={handleLogout}
					disabled={logoutPending}>{logoutPending ? "Logging out…" : "Logout"}</button
				>
			</p>
		{/if}
	</div>

	<!-- Bottom dock: actions + nav -->
	<div class="dock relative z-10 w-full px-4 pb-6 pt-6">
		<div class="relative mx-auto flex w-full max-w-sm flex-col gap-3">
			{#if !storeAuth.isLoggedIn}
				<!-- Guest: login + guest CTA -->
				<button
					class="btn pixel-corners w-full py-5 text-xl tracking-wider"
					onclick={() => storeNavigation.goto("auth")}
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
					onclick={() => storeNavigation.goto("lobbies")}
				>
					Play as Guest
				</button>
			{:else}
				<!-- Logged-in: primary CTA -->
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
							       {tile.available ? '' : 'opacity-50'}"
							style="--pc-fill: var(--surface); --pc-border: var(--border);"
							aria-disabled={!tile.available}
							onclick={() => {
								if (!tile.available) return;
								tile.action();
							}}
							aria-label="{tile.label}{tile.available ? '' : ' — coming soon'}"
						>
							<i class="pix {tile.icon} text-xl {tile.accent}"></i>
							<span class="font-tiny text-xs leading-tight text-text-h">{tile.label}</span>
							{#if !tile.available}
								<span class="font-tiny text-[0.6rem] leading-none text-accent/60">Soon</span>
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
				</nav>
				<nav class="flex items-center gap-3" aria-label="Social links">
					<a
						href="https://github.com/Eldyn/uni"
						target="_blank"
						rel="noopener noreferrer"
						class="social-icon"
						aria-label="GitHub"
					>
						<img src="/assets/social/github_icon.png" alt="GitHub" width="32" height="32" />
					</a>
					<a
						href="https://youtube.com/@play-uni"
						target="_blank"
						rel="noopener noreferrer"
						class="social-icon"
						aria-label="YouTube"
					>
						<img src="/assets/social/youtube_icon.png" alt="YouTube" width="32" height="32" />
					</a>
					<a
						href="https://tiktok.com/@the.uni.game"
						target="_blank"
						rel="noopener noreferrer"
						class="social-icon"
						aria-label="TikTok"
					>
						<img src="/assets/social/tiktok_icon.png" alt="TikTok" width="32" height="32" />
					</a>
					<a
						href="https://x.com/theunigamee"
						target="_blank"
						rel="noopener noreferrer"
						class="social-icon"
						aria-label="X"
					>
						<img src="/assets/social/x_icon.png" alt="X" width="32" height="32" />
					</a>
					<a
						href="https://bsky.app/profile/did:plc:pnfiqgr56esaantendnklouz"
						target="_blank"
						rel="noopener noreferrer"
						class="social-icon"
						aria-label="Bluesky"
					>
						<img src="/assets/social/bluesky_icon.png" alt="Bluesky" width="32" height="32" />
					</a>
					<a
						href="https://www.instagram.com/the.uni.game/"
						target="_blank"
						rel="noopener noreferrer"
						class="social-icon"
						aria-label="Instagram"
					>
						<img src="/assets/social/instagram_icon.png" alt="Instagram" width="32" height="32" />
					</a>
				</nav>
			</footer>
		</div>
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
		text-shadow: clamp(2px, 0.3vw, 4px) clamp(2px, 0.3vw, 4px) 0px var(--pixel-shadow);
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
