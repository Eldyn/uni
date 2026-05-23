<script lang="ts">
	import {
		validateEmail,
		validatePassword,
		validateUsername,
		validatePasswordMatch
	} from "$utils/validation";
	import { toastStore } from "$stores/ui.svelte";
	import { setLoading } from "$stores/auth.svelte";

	let usernameInput = $state("");
	let emailInput = $state("");
	let passwordInput = $state("");
	let confirmPasswordInput = $state("");
	let showPassword = $state(false);
	let showConfirmPassword = $state(false);
	let usernameError = $state("");
	let emailError = $state("");
	let passwordError = $state("");
	let confirmPasswordError = $state("");
	let isLoading = $state(false);

	let { onRegisterSuccess }: { onRegisterSuccess?: () => void } = $props();

	function validateForm(): boolean {
		usernameError = "";
		emailError = "";
		passwordError = "";
		confirmPasswordError = "";

		const usernameValidation = validateUsername(usernameInput);
		if (!usernameValidation.valid) {
			usernameError = usernameValidation.error || "";
			return false;
		}

		const emailValidation = validateEmail(emailInput);
		if (!emailValidation.valid) {
			emailError = emailValidation.error || "";
			return false;
		}

		const passwordValidation = validatePassword(passwordInput);
		if (!passwordValidation.valid) {
			passwordError = passwordValidation.error || "";
			return false;
		}

		const matchValidation = validatePasswordMatch(passwordInput, confirmPasswordInput);
		if (!matchValidation.valid) {
			confirmPasswordError = matchValidation.error || "";
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
			const response = await fetch(`${window.location.origin}/auth/register`, {
				method: "POST",
				headers: { "Content-Type": "application/json" },
				body: JSON.stringify({
					username: usernameInput,
					email: emailInput,
					password: passwordInput
				})
			});

			if (!response.ok) {
				if (response.status === 409) {
					toastStore.showError("Email or username already registered.");
				} else {
					toastStore.showError("Registration failed. Please try again.");
				}
				return;
			}

			toastStore.showSuccess("Registration successful! You can now log in.");
			onRegisterSuccess?.();
			// Clear form
			usernameInput = "";
			emailInput = "";
			passwordInput = "";
			confirmPasswordInput = "";
		} catch (error) {
			toastStore.showError("Network error. Please check your connection.");
		} finally {
			isLoading = false;
			setLoading(false);
		}
	}
</script>

<form onsubmit={handleSubmit} class="auth-form">
	<h2>Register</h2>

	<div class="form-group">
		<label for="register-username">Username:</label>
		<input
			id="register-username"
			type="text"
			bind:value={usernameInput}
			placeholder="Choose a username"
			disabled={isLoading}
			class:error={usernameError}
		/>
		{#if usernameError}
			<span class="error-text">{usernameError}</span>
		{/if}
	</div>

	<div class="form-group">
		<label for="register-email">Email:</label>
		<input
			id="register-email"
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
		<label for="register-password">Password:</label>
		<div class="password-input-group">
			<input
				id="register-password"
				type={showPassword ? "text" : "password"}
				bind:value={passwordInput}
				placeholder="Create a strong password"
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

	<div class="form-group">
		<label for="register-confirm-password">Confirm Password:</label>
		<div class="password-input-group">
			<input
				id="register-confirm-password"
				type={showConfirmPassword ? "text" : "password"}
				bind:value={confirmPasswordInput}
				placeholder="Confirm your password"
				disabled={isLoading}
				class:error={confirmPasswordError}
			/>
			<button
				type="button"
				class="password-toggle"
				onclick={() => (showConfirmPassword = !showConfirmPassword)}
				disabled={isLoading}
				aria-label={showConfirmPassword ? "Hide password" : "Show password"}
			>
				{showConfirmPassword ? "👁" : "👁‍🗨"}
			</button>
		</div>
		{#if confirmPasswordError}
			<span class="error-text">{confirmPasswordError}</span>
		{/if}
	</div>

	<button type="submit" disabled={isLoading} class="submit-button">
		{isLoading ? "Registering..." : "Register"}
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
