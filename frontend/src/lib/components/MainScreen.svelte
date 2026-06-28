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
</script>

<div class="screen-container">
	<div class="doodle-bg"></div>

	<header class="logo-container">
		<TextEffects
			text="UNI!"
			effect="undulate"
			class="logo-text title-hero"
			font="var(--heading)"
			amplitude={20}
			speed={1}
			frequency={0.15}
		/>
	</header>

	<main class="button-container">
		{#if !storeAuth.isLoggedIn}
			<button class="btn menu-btn pixel-corners" onclick={() => storeNavigation.goto("auth")}>
				Login
			</button>
		{:else}
			<div class="logged-in-menu">
				<button
					class="btn menu-btn btn-wide pixel-corners"
					onclick={() => storeNavigation.goto("lobbies")}
				>
					Browse Lobbies
				</button>
				<div class="secondary-buttons">
					<button class="btn menu-btn pixel-corners" onclick={() => storeNavigation.goto("stats")}>
						Stats
					</button>
					<button class="btn menu-btn pixel-corners" onclick={handleLogout} disabled={logoutPending}
						>Logout</button
					>
				</div>
			</div>
		{/if}
	</main>
</div>

<style>
	.doodle-bg {
		position: fixed;
		inset: 0;
		background-image: url("/assets/bg_main.png");
		background-size: cover;
		z-index: 0;
	}

	.screen-container {
		position: relative;
		display: flex;
		flex-direction: column;
		justify-content: flex-end;
		align-items: center;

		gap: 2rem;
		min-height: 100vh;
		padding-bottom: 6rem;
		background: var(--bg);
		overflow: hidden;

		-webkit-font-smoothing: none;
		-moz-osx-font-smoothing: grayscale;
		font-smooth: never;
	}

	@keyframes fallAndFade {
		0% {
			transform: translateY(0);
			opacity: 0;
		}
		8% {
			opacity: 0.45;
		}
		92% {
			opacity: 0.45;
		}
		100% {
			transform: translateY(125vh);
			opacity: 0;
		}
	}

	@keyframes spin3D {
		0% {
			transform: rotateX(var(--initial-rx)) rotateY(0deg) rotateZ(var(--rotate-z))
				scale(var(--scale));
		}
		100% {
			transform: rotateX(calc(var(--initial-rx) + 40deg)) rotateY(720deg)
				rotateZ(calc(var(--rotate-z) + 150deg)) scale(var(--scale));
		}
	}

	.logo-container,
	.button-container {
		position: relative;
		z-index: 1;
	}

	.logo-container {
		display: flex;
		justify-content: center;
		color: #ffffff;

		margin: 0;
		text-align: center;
		display: flex;
		align-items: center;
		justify-content: center;
		gap: 0.4rem;
	}

	/* Logo sizing/stroke/shadow live in the shared .title-hero class (app.css),
	   kept in sync with the #seo-splash hero in index.html. Only the per-char
	   gap is logo-specific. */
	.logo-container :global(.logo-text) {
		gap: 0.4rem;
	}

	.button-container {
		display: flex;
		justify-content: center;
		align-items: center;
		margin-bottom: 3rem;
	}

	.logged-in-menu {
		display: flex;
		flex-direction: column;
		gap: 1.5rem;
	}

	.secondary-buttons {
		display: flex;
		justify-content: center;
		gap: 1.5rem;
	}

	.btn-wide {
		width: 100%;
	}

	/* Hero-sized variant of the shared flat .btn */
	.menu-btn {
		padding: 1.5rem 2.5rem;
		font-size: 1.35rem;
		font-weight: 900;
		letter-spacing: 1.5px;
		line-height: 1.3;
	}
</style>
