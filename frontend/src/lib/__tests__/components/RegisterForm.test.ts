import { describe, it, expect, vi, beforeEach } from "vitest";
import { render, screen, fireEvent } from "@testing-library/svelte";
import RegisterForm from "$lib/components/auth/RegisterForm.svelte";
import { storeAuth } from "$stores/auth.svelte";
import { storeVerify } from "$stores/verify.svelte";

vi.mock("$utils/censor.svelte", () => ({
	loadCensorData: vi.fn().mockResolvedValue(undefined),
	censorText: vi.fn((text: string) => text)
}));

vi.mock("$stores/auth.svelte", () => ({
	storeAuth: {
		isLoading: false,
		register: vi.fn()
	}
}));

vi.mock("$stores/verify.svelte", () => ({
	storeVerify: {
		isSending: false,
		isConfirming: false,
		cooldownSeconds: 0,
		error: "",
		requestCode: vi.fn().mockResolvedValue(true),
		confirmCode: vi.fn().mockResolvedValue(true),
		reset: vi.fn()
	}
}));

vi.mock("$stores/i18n.svelte", () => ({
	storeI18n: {
		locale: "en"
	}
}));

vi.mock("$lib/paraglide/messages.js", () => ({
	verify_title: vi.fn(() => "Verify your email"),
	verify_subtitle: vi.fn(() => "Enter the code we sent to your email to confirm your address."),
	verify_code_input_label: vi.fn(() => "Verification code"),
	verify_submit_button: vi.fn(() => "Verify"),
	verify_resend_button: vi.fn(() => "Resend code"),
	verify_resend_cooldown: vi.fn(({ seconds }: { seconds: number }) => `Resend in ${seconds}s`),
	verify_skip_link: vi.fn(() => "Skip for now"),
	error_verify_wrong_code: vi.fn(() => "Wrong code. Try again.")
}));

describe("RegisterForm", () => {
	beforeEach(() => {
		vi.clearAllMocks();
		storeVerify.error = "";
		storeVerify.isSending = false;
		storeVerify.isConfirming = false;
		storeVerify.cooldownSeconds = 0;
	});

	async function fillAndSubmitRegisterForm(email = "test@example.com") {
		const usernameInput = screen.getByLabelText(/username:/i);
		const emailInput = screen.getByLabelText(/email:/i);
		const passwordInput = screen.getByLabelText(/^password:/i);
		const confirmInput = screen.getByLabelText(/confirm password:/i);

		await fireEvent.input(usernameInput, { target: { value: "testuser" } });
		await fireEvent.input(emailInput, { target: { value: email } });
		await fireEvent.input(passwordInput, { target: { value: "password123" } });
		await fireEvent.input(confirmInput, { target: { value: "password123" } });

		const submitBtn = screen.getByRole("button", { name: /^register$/i });
		await fireEvent.click(submitBtn);
	}

	async function triggerVerifiedCallback() {
		const boxes = screen.getAllByLabelText(/digit/i);
		for (let i = 0; i < 6; i++) {
			await fireEvent.input(boxes[i], { target: { value: String(i + 1) } });
		}
		const submitBtn = screen.getByRole("button", { name: /submit/i });
		await fireEvent.click(submitBtn);
	}

	it("shows verify step after successful registration", async () => {
		vi.mocked(storeAuth.register).mockResolvedValueOnce(true as any);
		render(RegisterForm, { props: {} });
		await fillAndSubmitRegisterForm();
		expect(screen.getByTestId("verify-code-form")).toBeInTheDocument();
	});

	it("completing verify calls onRegisterSuccess once", async () => {
		const onRegisterSuccess = vi.fn();
		vi.mocked(storeAuth.register).mockResolvedValueOnce(true as any);
		render(RegisterForm, { props: { onRegisterSuccess } });
		await fillAndSubmitRegisterForm();
		await triggerVerifiedCallback();
		expect(onRegisterSuccess).toHaveBeenCalledTimes(1);
	});

	it("skipping verify step calls onRegisterSuccess once", async () => {
		const onRegisterSuccess = vi.fn();
		vi.mocked(storeAuth.register).mockResolvedValueOnce(true as any);
		render(RegisterForm, { props: { onRegisterSuccess } });
		await fillAndSubmitRegisterForm();
		const skipBtn = screen.getByRole("button", { name: /skip for now/i });
		await fireEvent.click(skipBtn);
		expect(onRegisterSuccess).toHaveBeenCalledTimes(1);
	});

	it("requests verification code upon transitioning to verify step", async () => {
		vi.mocked(storeAuth.register).mockResolvedValueOnce(true as any);
		render(RegisterForm, { props: {} });
		await fillAndSubmitRegisterForm("user@example.com");
		expect(storeVerify.requestCode).toHaveBeenCalledTimes(1);
	});
});
