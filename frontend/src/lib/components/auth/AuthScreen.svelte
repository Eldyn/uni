<script lang="ts">
	import LoginForm from "./LoginForm.svelte";
	import RegisterForm from "./RegisterForm.svelte";

	let activeTab = $state<"login" | "register">("login");

	let { onAuthSuccess }: { onAuthSuccess: () => void } = $props();
</script>

<div class="auth-screen">
	<section class="auth-container pixel-corners w-full">
		<div class="auth-tabs">
			<button
				type="button"
				class="tab-button"
				class:active={activeTab === "login"}
				onclick={() => (activeTab = "login")}
			>
				Login
			</button>
			<button
				type="button"
				class="tab-button"
				class:active={activeTab === "register"}
				onclick={() => (activeTab = "register")}
			>
				Register
			</button>
		</div>

		<div class="auth-content">
			{#if activeTab === "login"}
				<LoginForm onLoginSuccess={onAuthSuccess} />
			{:else}
				<RegisterForm
					onRegisterSuccess={() => {
						activeTab = "login";
					}}
				/>
			{/if}
		</div>
	</section>
</div>

<style>
	.auth-screen {
		display: flex;
		align-items: center;
		justify-content: center;
		min-height: 100vh;
		padding: 20px;
		font-family: "Pixel";
		background-image: url("/assets/bg_main.png");
		image-rendering: pixelated;
		background-size: cover;
	}

	.auth-tabs {
		display: flex;
		gap: 0;
		margin-bottom: 24px;
		border-bottom: 1px solid var(--border);
	}

	.tab-button {
		flex: 1;
		padding: 12px;
		background: none;
		border: none;
		font-size: 16px;
		font-family: "Pixel";
		font-weight: 500;
		letter-spacing: 1px;
		color: var(--text);
		cursor: pointer;
		border-bottom: 2px solid transparent;
		transition: all 0.2s;
	}

	.tab-button:hover {
		color: var(--accent);
	}

	.tab-button.active {
		color: var(--accent);
		border-bottom-color: var(--accent);
	}

	.auth-content {
		animation: fadeIn 0.3s ease-out;
	}

	@keyframes fadeIn {
		from {
			opacity: 0;
		}
		to {
			opacity: 1;
		}
	}

	@media (max-width: 1024px) {
		.auth-container {
			padding: 24px;
		}
	}
</style>
