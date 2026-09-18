import { describe, it, expect, vi, beforeEach } from "vitest";
import { render, screen, fireEvent } from "@testing-library/svelte";
import { storeNavigation } from "$stores/navigation.svelte";

vi.mock("$stores/navigation.svelte", () => ({
	storeNavigation: {
		isVerifyModalOpen: true,
		activeVerifyCode: null,
		closeVerifyModal: vi.fn()
	}
}));

vi.mock("$stores/auth.svelte", () => ({
	storeAuth: {
		isLoggedIn: true,
		emailVerified: false
	}
}));

vi.mock("$stores/verify.svelte", () => ({
	storeVerify: {
		cooldownSeconds: 0,
		error: "",
		isSending: false,
		isConfirming: false,
		confirmCode: vi.fn(),
		requestCode: vi.fn(),
		reset: vi.fn()
	}
}));

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
	settings_close: vi.fn(() => "Close")
}));

import VerifyModal from "$components/auth/VerifyModal.svelte";

describe("VerifyModal", () => {
	beforeEach(() => {
		vi.restoreAllMocks();
		storeNavigation.isVerifyModalOpen = true;
	});

	it("renders verify modal with title and form", () => {
		render(VerifyModal);
		expect(screen.getByText("Verify your email")).toBeInTheDocument();
		expect(screen.getAllByRole("textbox")).toHaveLength(6);
	});

	it("calls closeVerifyModal when close button is clicked", async () => {
		render(VerifyModal);
		const closeButton = screen.getByRole("button", { name: "Close" });
		await fireEvent.click(closeButton);
		expect(storeNavigation.closeVerifyModal).toHaveBeenCalled();
	});

	it("calls closeVerifyModal when skip is clicked", async () => {
		render(VerifyModal);
		const skipButton = screen.getByRole("button", { name: "Skip for now" });
		await fireEvent.click(skipButton);
		expect(storeNavigation.closeVerifyModal).toHaveBeenCalled();
	});
});
