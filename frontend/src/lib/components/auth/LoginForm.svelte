<script lang="ts">
	import { validateEmail } from "$utils/validation";
	import { toastStore } from "$stores/ui.svelte";
	import { setLoggedIn, setLoading } from "$stores/auth.svelte";

	let emailInput = $state("");
	let passwordInput = $state("");
	let showPassword = $state(false);
	let emailError = $state("");
	let passwordError = $state("");
	let isLoading = $state(false);

	let { onLoginSuccess }: { onLoginSuccess?: () => void } = $props();

	function validateForm(): boolean {
		emailError = "";
		passwordError = "";

		const emailValidation = validateEmail(emailInput);
		if (!emailValidation.valid) {
			emailError = emailValidation.error || "";
			return false;
		}

		// Password doesn't need to match the full strength criteria for login
		if (!passwordInput) {
			passwordError = "Password is required.";
			return false;
		}

		return true;
	}

	async function handleSubmit(event: SubmitEvent) {
		event.preventDefault();

		if (!validateForm()) {
			return;
		}

		isLoading = true;
		setLoading(true);

		try {
			const response = await fetch(`${window.location.origin}/auth/login`, {
				method: "POST",
				headers: { "Content-Type": "application/json" },
				body: JSON.stringify({
					email: emailInput,
					password: passwordInput
				})
			});

			if (!response.ok) {
				const userMessage =
					response.status === 401
						? "Invalid email or password."
						: "Login failed. Please try again.";
				toastStore.showError(userMessage);
				return;
			}

			const data = await response.json();
			setLoggedIn(data.username, emailInput);
			toastStore.showSuccess("Login successful!");
			onLoginSuccess?.();
		} catch (error) {
			toastStore.showError("Network error. Please check your connection.");
		} finally {
			isLoading = false;
			setLoading(false);
		}
	}
</script>

<form onsubmit={handleSubmit} class="auth-form">
	<h2>Login</h2>

	<div class="form-group">
		<label for="login-email">Email:</label>
		<input
			id="login-email"
			type="email"
			bind:value={emailInput}
			placeholder="Enter your email"
			disabled={isLoading}
			class:error={emailError}
		/>
		{#if emailError}
			<span class="error-text">{emailError}</span>
		{/if}
	</div>

	<div class="form-group">
		<label for="login-password">Password:</label>
		<div class="password-input-group">
			<input
				id="login-password"
				type={showPassword ? "text" : "password"}
				bind:value={passwordInput}
				placeholder="Enter your password"
				disabled={isLoading}
				class:error={passwordError}
			/>
			<button
				type="button"
				class="password-toggle"
				onclick={() => (showPassword = !showPassword)}
				disabled={isLoading}
				aria-label={showPassword ? "Hide password" : "Show password"}
			>
				{showPassword ? "👁" : "👁‍🗨"}
			</button>
		</div>
		{#if passwordError}
			<span class="error-text">{passwordError}</span>
		{/if}
	</div>

	<button type="submit" disabled={isLoading} class="submit-button">
		{isLoading ? "Logging in..." : "Login"}
	</button>
</form>

<style>
	.auth-form {
		display: flex;
		flex-direction: column;
		gap: 16px;
	}

	h2 {
		margin: 0 0 16px;
		font-size: 20px;
		color: var(--text-h);
	}

	.form-group {
		display: flex;
		flex-direction: column;
		gap: 6px;
	}

	label {
		font-size: 14px;
		font-weight: 500;
		color: var(--text-h);
	}

	input {
		padding: 10px 12px;
		border: 2px solid var(--border);
		border-radius: 6px;
		font-size: 14px;
		color: var(--text-h);
		background: var(--bg);
		transition: border-color 0.2s;
	}

	input:focus {
		outline: none;
		border-color: var(--accent);
	}

	input:disabled {
		opacity: 0.6;
		cursor: not-allowed;
	}

	input.error {
		border-color: #dc2626;
	}

	.password-input-group {
		position: relative;
		display: flex;
		align-items: center;
	}

	.password-input-group input {
		flex: 1;
		padding-right: 40px;
	}

	.password-toggle {
		position: absolute;
		right: 10px;
		background: none;
		border: none;
		cursor: pointer;
		font-size: 18px;
		padding: 4px 8px;
		opacity: 0.6;
		transition: opacity 0.2s;
	}

	.password-toggle:hover:not(:disabled) {
		opacity: 1;
	}

	.password-toggle:disabled {
		cursor: not-allowed;
	}

	.error-text {
		font-size: 12px;
		color: #dc2626;
	}

	.submit-button {
		padding: 10px 16px;
		background: var(--accent);
		color: white;
		border: none;
		border-radius: 6px;
		font-size: 14px;
		font-weight: 500;
		cursor: pointer;
		transition: opacity 0.2s;
	}

	.submit-button:hover:not(:disabled) {
		opacity: 0.9;
	}

	.submit-button:disabled {
		opacity: 0.6;
		cursor: not-allowed;
	}
</style>
