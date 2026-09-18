import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { render, screen, fireEvent } from "@testing-library/svelte";
import { storeVerify } from "$stores/verify.svelte";

vi.mock("$stores/toast.svelte", () => ({
	storeToast: {
		success: vi.fn(),
		error: vi.fn()
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
	error_verify_wrong_code: vi.fn(() => "Wrong code. Try again."),
	error_verify_too_many_attempts: vi.fn(() => "Too many attempts. Request a new code."),
	error_verify_rate_limited: vi.fn(() => "You're requesting codes too fast, please slow down.")
}));

import VerifyCodeForm from "$lib/components/auth/VerifyCodeForm.svelte";

describe("VerifyCodeForm", () => {
	beforeEach(() => {
		vi.restoreAllMocks();
		storeVerify.reset();
		storeVerify.cooldownSeconds = 0;
		storeVerify.error = "";
		storeVerify.isSending = false;
		storeVerify.isConfirming = false;
	});

	afterEach(() => {
		storeVerify.reset();
	});

	it("typing 6 digits enables submit and auto-advances focus", async () => {
		render(VerifyCodeForm, { props: {} });
		const boxes = screen.getAllByRole("textbox");
		expect(boxes).toHaveLength(6);

		for (let i = 0; i < 6; i++) {
			await fireEvent.input(boxes[i], { target: { value: String(i) } });
		}
		expect(screen.getByRole("button", { name: /submit/i })).toBeEnabled();
	});

	it("auto-advances focus to next box on digit input", async () => {
		render(VerifyCodeForm, { props: {} });
		const boxes = screen.getAllByRole("textbox");

		boxes[0].focus();
		expect(document.activeElement).toBe(boxes[0]);

		await fireEvent.input(boxes[0], { target: { value: "1" } });
		expect(document.activeElement).toBe(boxes[1]);

		await fireEvent.input(boxes[1], { target: { value: "2" } });
		expect(document.activeElement).toBe(boxes[2]);
	});

	it("backspace steps back to previous box when current box is empty", async () => {
		render(VerifyCodeForm, { props: {} });
		const boxes = screen.getAllByRole("textbox");

		boxes[1].focus();
		expect(document.activeElement).toBe(boxes[1]);

		await fireEvent.keyDown(boxes[1], { key: "Backspace" });
		expect(document.activeElement).toBe(boxes[0]);
	});

	it("pasting 6 digits fills all boxes", async () => {
		render(VerifyCodeForm, { props: {} });
		const boxes = screen.getAllByRole("textbox");
		await fireEvent.click(boxes[0]);
		await fireEvent.paste(boxes[0], {
			clipboardData: {
				getData: () => "123456"
			}
		});
		boxes.forEach((box, i) => expect(box).toHaveValue(String(i + 1 === 6 ? 6 : i + 1)[0]));
	});

	it("shows inline error on wrong code without navigating", async () => {
		vi.spyOn(storeVerify, "confirmCode").mockResolvedValueOnce(false);
		storeVerify.error = "error_verify_wrong_code";
		render(VerifyCodeForm, { props: {} });

		const boxes = screen.getAllByRole("textbox");
		for (let i = 0; i < 6; i++) {
			await fireEvent.input(boxes[i], { target: { value: "1" } });
		}
		const submitBtn = screen.getByRole("button", { name: /submit/i });
		await fireEvent.click(submitBtn);

		expect(screen.getByText(/wrong code/i)).toBeInTheDocument();
	});

	it("autoSubmit with initialCode fires confirm once on mount", async () => {
		const spy = vi.spyOn(storeVerify, "confirmCode").mockResolvedValueOnce(true);
		render(VerifyCodeForm, {
			props: { initialCode: "123456", autoSubmit: true, email: "a@b.com" }
		});
		await vi.waitFor(() => expect(spy).toHaveBeenCalledTimes(1));
		expect(spy).toHaveBeenCalledWith("123456", "a@b.com");
	});

	it("populates boxes with initialCode without auto-submitting if autoSubmit is false", async () => {
		const spy = vi.spyOn(storeVerify, "confirmCode");
		render(VerifyCodeForm, { props: { initialCode: "654321", autoSubmit: false } });
		const boxes = screen.getAllByRole("textbox");
		expect(boxes[0]).toHaveValue("6");
		expect(boxes[1]).toHaveValue("5");
		expect(boxes[2]).toHaveValue("4");
		expect(boxes[3]).toHaveValue("3");
		expect(boxes[4]).toHaveValue("2");
		expect(boxes[5]).toHaveValue("1");
		expect(spy).not.toHaveBeenCalled();
	});

	it("calls onVerified when code confirmation succeeds", async () => {
		vi.spyOn(storeVerify, "confirmCode").mockResolvedValueOnce(true);
		const onVerified = vi.fn();
		render(VerifyCodeForm, { props: { onVerified, email: "test@example.com" } });

		const boxes = screen.getAllByRole("textbox");
		for (let i = 0; i < 6; i++) {
			await fireEvent.input(boxes[i], { target: { value: "8" } });
		}
		const submitBtn = screen.getByRole("button", { name: /submit/i });
		await fireEvent.click(submitBtn);

		expect(storeVerify.confirmCode).toHaveBeenCalledWith("888888", "test@example.com");
		expect(onVerified).toHaveBeenCalledTimes(1);
	});

	it("resend button triggers requestCode when clicked and reflects cooldown timer", async () => {
		const spy = vi.spyOn(storeVerify, "requestCode").mockResolvedValueOnce(true);
		const { rerender } = render(VerifyCodeForm, { props: {} });
		const resendBtn = screen.getByRole("button", { name: /resend code/i });
		expect(resendBtn).toBeEnabled();

		await fireEvent.click(resendBtn);
		expect(spy).toHaveBeenCalledTimes(1);

		storeVerify.cooldownSeconds = 30;
		await rerender({});
		const cooldownBtn = screen.getByRole("button", { name: /resend in 30s/i });
		expect(cooldownBtn).toBeDisabled();
	});

	it('calls onSkip when "Skip for now" button is clicked', async () => {
		const onSkip = vi.fn();
		render(VerifyCodeForm, { props: { onSkip } });
		const skipBtn = screen.getByRole("button", { name: /skip for now/i });
		await fireEvent.click(skipBtn);
		expect(onSkip).toHaveBeenCalledTimes(1);
	});

	it("submit button remains disabled when fewer than 6 digits are entered", async () => {
		render(VerifyCodeForm, { props: {} });
		const boxes = screen.getAllByRole("textbox");
		for (let i = 0; i < 5; i++) {
			await fireEvent.input(boxes[i], { target: { value: String(i) } });
		}
		const submitBtn = screen.getByRole("button", { name: /submit/i });
		expect(submitBtn).toBeDisabled();
	});
});
