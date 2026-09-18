<script lang="ts">
	import { onMount } from "svelte";
	import { storeVerify } from "$stores/verify.svelte";
	import { storeI18n } from "$stores/i18n.svelte";
	import * as m from "$lib/paraglide/messages.js";

	let {
		email,
		initialCode = "",
		autoSubmit = false,
		onVerified,
		onSkip
	}: {
		email?: string;
		initialCode?: string;
		autoSubmit?: boolean;
		onVerified?: () => void;
		onSkip?: () => void;
	} = $props();

	let digits = $state<string[]>(["", "", "", "", "", ""]);
	let inputRefs = $state<HTMLInputElement[]>([]);

	$effect(() => {
		if (initialCode) {
			const clean = initialCode.replace(/\D/g, "").slice(0, 6);
			for (let i = 0; i < 6; i++) {
				digits[i] = clean[i] ?? "";
			}
		}
	});

	onMount(async () => {
		if (autoSubmit && initialCode && initialCode.length === 6 && /^\d{6}$/.test(initialCode)) {
			const success = await storeVerify.confirmCode(initialCode, email);
			if (success && onVerified) {
				onVerified();
			}
		}
	});

	const code = $derived(digits.join(""));
	const isValid = $derived(code.length === 6 && /^\d{6}$/.test(code));

	const displayError = $derived.by(() => {
		if (!storeVerify.error) return "";
		if (storeVerify.error === "error_verify_wrong_code") {
			return typeof m.error_verify_wrong_code === "function"
				? m.error_verify_wrong_code({}, { locale: storeI18n.locale })
				: "Wrong code";
		}
		return storeVerify.error;
	});

	function handleInput(event: Event, index: number) {
		const target = event.target as HTMLInputElement;
		const raw = target.value;
		const cleaned = raw.replace(/\D/g, "");

		if (!cleaned) {
			digits[index] = "";
			target.value = "";
			return;
		}

		if (cleaned.length === 1) {
			digits[index] = cleaned;
			target.value = cleaned;
			if (index < 5) {
				inputRefs[index + 1]?.focus();
			}
		} else {
			const chars = cleaned.split("");
			for (let i = 0; i < chars.length && index + i < 6; i++) {
				digits[index + i] = chars[i];
				if (inputRefs[index + i]) {
					inputRefs[index + i].value = chars[i];
				}
			}
			const nextIdx = Math.min(index + chars.length, 5);
			inputRefs[nextIdx]?.focus();
		}
	}

	function handleKeyDown(event: KeyboardEvent, index: number) {
		if (event.key === "Backspace") {
			if (!digits[index] && index > 0) {
				event.preventDefault();
				digits[index - 1] = "";
				if (inputRefs[index - 1]) {
					inputRefs[index - 1].value = "";
					inputRefs[index - 1].focus();
				}
			} else {
				digits[index] = "";
			}
		} else if (event.key === "ArrowLeft" && index > 0) {
			event.preventDefault();
			inputRefs[index - 1]?.focus();
		} else if (event.key === "ArrowRight" && index < 5) {
			event.preventDefault();
			inputRefs[index + 1]?.focus();
		}
	}

	function handlePaste(event: ClipboardEvent) {
		event.preventDefault();
		const clip = event.clipboardData;
		let text = "";
		if (clip && typeof clip.getData === "function") {
			text = clip.getData("text") || clip.getData("text/plain") || (clip as any).getData?.();
		}
		if (!text) return;
		const clean = String(text).trim().replace(/\D/g, "").slice(0, 6);
		if (!clean) return;

		for (let i = 0; i < 6; i++) {
			digits[i] = clean[i] ?? "";
			if (inputRefs[i]) {
				inputRefs[i].value = digits[i];
			}
		}
		const nextIdx = Math.min(clean.length, 5);
		inputRefs[nextIdx]?.focus();
	}

	async function handleSubmit(event?: SubmitEvent) {
		event?.preventDefault();
		if (!isValid || storeVerify.isConfirming) return;
		const success = await storeVerify.confirmCode(code, email);
		if (success && onVerified) {
			onVerified();
		}
	}

	async function handleResend() {
		if (storeVerify.cooldownSeconds > 0 || storeVerify.isSending) return;
		await storeVerify.requestCode();
	}
</script>

<form onsubmit={handleSubmit} class="verify-form auth-form" data-testid="verify-code-form">
	<div class="verify-header">
		<h2 class="verify-title font-pixel">
			{typeof m.verify_title === "function"
				? m.verify_title({}, { locale: storeI18n.locale })
				: "Verify your email"}
		</h2>
		<p class="verify-subtitle">
			{typeof m.verify_subtitle === "function"
				? m.verify_subtitle({}, { locale: storeI18n.locale })
				: "Enter the code we sent to your email to confirm your address."}
		</p>
	</div>

	<div class="code-input-group">
		<label for="code-box-0" class="code-label font-pixel text-text-h">
			{typeof m.verify_code_input_label === "function"
				? m.verify_code_input_label({}, { locale: storeI18n.locale })
				: "Verification code"}
		</label>
		<div class="code-boxes" onpaste={handlePaste}>
			{#each digits as digit, i}
				<input
					id="code-box-{i}"
					type="text"
					inputmode="numeric"
					maxlength="1"
					pattern="[0-9]"
					autocomplete={i === 0 ? "one-time-code" : "off"}
					class="code-box"
					class:error={!!displayError}
					value={digit}
					bind:this={inputRefs[i]}
					oninput={(e) => handleInput(e, i)}
					onkeydown={(e) => handleKeyDown(e, i)}
					onpaste={handlePaste}
					aria-label={`Digit ${i + 1}`}
					disabled={storeVerify.isConfirming}
				/>
			{/each}
		</div>
	</div>

	{#if displayError}
		<p class="error-message text-danger" role="alert">
			{displayError}
		</p>
	{/if}

	<button
		type="submit"
		class="btn pixel-corners"
		disabled={!isValid || storeVerify.isConfirming}
		aria-label="{typeof m.verify_submit_button === 'function'
			? m.verify_submit_button({}, { locale: storeI18n.locale })
			: 'Verify'} (Submit)"
	>
		{storeVerify.isConfirming
			? "..."
			: typeof m.verify_submit_button === "function"
				? m.verify_submit_button({}, { locale: storeI18n.locale })
				: "Verify"}
	</button>

	<button
		type="button"
		class="btn-ghost pixel-corners resend-btn"
		disabled={storeVerify.cooldownSeconds > 0 || storeVerify.isSending}
		onclick={handleResend}
	>
		{storeVerify.cooldownSeconds > 0
			? typeof m.verify_resend_cooldown === "function"
				? m.verify_resend_cooldown(
						{ seconds: storeVerify.cooldownSeconds },
						{ locale: storeI18n.locale }
					)
				: `Resend in ${storeVerify.cooldownSeconds}s`
			: typeof m.verify_resend_button === "function"
				? m.verify_resend_button({}, { locale: storeI18n.locale })
				: "Resend code"}
	</button>

	<button type="button" class="skip-link font-pixel" onclick={() => onSkip?.()}>
		{typeof m.verify_skip_link === "function"
			? m.verify_skip_link({}, { locale: storeI18n.locale })
			: "Skip for now"}
	</button>
</form>

<style>
	.verify-form {
		display: flex;
		flex-direction: column;
		gap: 16px;
		align-items: stretch;
	}

	.verify-header {
		text-align: center;
		margin-bottom: 4px;
	}

	.verify-title {
		font-size: 18px;
		font-weight: bold;
		color: var(--text-h);
		margin: 0 0 6px 0;
	}

	.verify-subtitle {
		font-size: 13px;
		color: var(--text);
		margin: 0;
		line-height: 1.4;
	}

	.code-input-group {
		display: flex;
		flex-direction: column;
		gap: 8px;
		align-items: center;
	}

	.code-label {
		font-size: 14px;
		font-weight: 500;
		align-self: flex-start;
	}

	.code-boxes {
		display: flex;
		justify-content: center;
		gap: 8px;
		width: 100%;
	}

	.code-box {
		width: 44px;
		height: 52px;
		text-align: center;
		font-size: 24px;
		background: var(--surface);
		border: 2px solid var(--border);
		box-shadow: var(--elevation-1);
		clip-path: var(--notch-clip);
		color: var(--text-h);
		font-family: var(--pixel);
		outline: none;
		transition: border-color 0.15s ease;
	}

	.code-box:focus {
		border-color: var(--accent);
	}

	.code-box.error {
		border-color: var(--danger);
	}

	.code-box.error:focus {
		border-color: var(--accent);
	}

	.error-message {
		font-size: 12px;
		text-align: center;
		margin: 0;
	}

	.resend-btn {
		font-size: 13px;
		padding: 0.5rem 1rem;
	}

	.skip-link {
		background: none;
		border: none;
		color: var(--text);
		text-decoration: underline;
		cursor: pointer;
		font-size: 13px;
		padding: 4px 8px;
		align-self: center;
		transition: color 0.15s ease;
	}

	.skip-link:hover {
		color: var(--text-h);
	}
</style>
