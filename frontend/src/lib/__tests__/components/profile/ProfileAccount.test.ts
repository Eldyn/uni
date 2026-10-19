import { describe, it, expect, vi, beforeEach } from "vitest";
import { render, screen, fireEvent } from "@testing-library/svelte";
import { storeAuth } from "$lib/stores/auth.svelte";
import { storeNavigation } from "$lib/stores/navigation.svelte";
import { storeVerify } from "$stores/verify.svelte";
import ProfileAccount from "$components/profile/ProfileAccount.svelte";

vi.mock("$lib/stores/navigation.svelte", () => ({
	storeNavigation: { goto: vi.fn(), openSettings: vi.fn(), activeVerifyCode: null }
}));
vi.mock("$lib/stores/auth.svelte", () => ({
	storeAuth: {
		username: "eldyn",
		avatar: "",
		isLoggedIn: true,
		emailVerified: false,
		onLoggedOut: vi.fn()
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
		reset: vi.fn(),
		dispose: vi.fn()
	}
}));

describe("ProfileAccount", () => {
	beforeEach(() => {
		storeNavigation.activeVerifyCode = null;
		vi.mocked(storeVerify).confirmCode.mockReset();
		vi.mocked(storeVerify).confirmCode.mockResolvedValue(true);
	});

	it("shows VERIFY button for unverified logged-in accounts", () => {
		vi.mocked(storeAuth).isLoggedIn = true;
		vi.mocked(storeAuth).emailVerified = false;
		render(ProfileAccount);
		expect(screen.getByRole("button", { name: /verify/i })).toBeInTheDocument();
	});

	it("hides VERIFY button for verified accounts and guests", () => {
		vi.mocked(storeAuth).isLoggedIn = true;
		vi.mocked(storeAuth).emailVerified = true;
		const { unmount } = render(ProfileAccount);
		expect(screen.queryByRole("button", { name: /verify/i })).not.toBeInTheDocument();
		unmount();

		vi.mocked(storeAuth).isLoggedIn = false;
		vi.mocked(storeAuth).emailVerified = false;
		render(ProfileAccount);
		expect(screen.queryByRole("button", { name: /verify/i })).not.toBeInTheDocument();
	});

	it("toggles verify code form when clicking VERIFY button", async () => {
		vi.mocked(storeAuth).isLoggedIn = true;
		vi.mocked(storeAuth).emailVerified = false;
		render(ProfileAccount);
		const btn = screen.getByRole("button", { name: /verify/i });
		expect(btn).toHaveAttribute("aria-expanded", "false");
		expect(screen.queryByTestId("verify-code-form")).not.toBeInTheDocument();
		await fireEvent.click(btn);
		expect(btn).toHaveAttribute("aria-expanded", "true");
		expect(screen.getByTestId("verify-code-form")).toBeInTheDocument();
		await fireEvent.click(btn);
		expect(btn).toHaveAttribute("aria-expanded", "false");
		expect(screen.queryByTestId("verify-code-form")).not.toBeInTheDocument();
	});

	it("closes verify code form when onVerified callback is triggered", async () => {
		vi.mocked(storeAuth).isLoggedIn = true;
		vi.mocked(storeAuth).emailVerified = false;
		render(ProfileAccount);
		const btn = screen.getByRole("button", { name: /verify/i });
		await fireEvent.click(btn);
		expect(screen.getByTestId("verify-code-form")).toBeInTheDocument();

		const boxes = screen.getAllByLabelText(/digit/i);
		for (let i = 0; i < 6; i++) {
			await fireEvent.input(boxes[i], { target: { value: String(i + 1) } });
		}
		const submitBtn = screen.getByRole("button", { name: /verify.*submit/i });
		await fireEvent.click(submitBtn);

		expect(screen.queryByTestId("verify-code-form")).not.toBeInTheDocument();
		expect(btn).toHaveAttribute("aria-expanded", "false");
	});

	it("closes verify code form when onSkip callback is triggered", async () => {
		vi.mocked(storeAuth).isLoggedIn = true;
		vi.mocked(storeAuth).emailVerified = false;
		render(ProfileAccount);
		const btn = screen.getByRole("button", { name: /verify/i });
		await fireEvent.click(btn);
		expect(screen.getByTestId("verify-code-form")).toBeInTheDocument();

		const skipBtn = screen.getByRole("button", { name: /skip for now/i });
		await fireEvent.click(skipBtn);

		expect(screen.queryByTestId("verify-code-form")).not.toBeInTheDocument();
		expect(btn).toHaveAttribute("aria-expanded", "false");
	});

	it("auto-opens verify code form with autoSubmit when activeVerifyCode is set", async () => {
		vi.mocked(storeAuth).isLoggedIn = true;
		vi.mocked(storeAuth).emailVerified = false;
		storeNavigation.activeVerifyCode = "123456";

		render(ProfileAccount);

		expect(screen.getByTestId("verify-code-form")).toBeInTheDocument();
		await vi.waitFor(() => {
			expect(storeVerify.confirmCode).toHaveBeenCalledWith("123456", undefined);
		});
	});

	it("clears activeVerifyCode when onVerified callback is triggered", async () => {
		vi.mocked(storeAuth).isLoggedIn = true;
		vi.mocked(storeAuth).emailVerified = false;
		storeNavigation.activeVerifyCode = "123456";
		vi.mocked(storeVerify).confirmCode.mockResolvedValueOnce(false);

		render(ProfileAccount);

		expect(screen.getByTestId("verify-code-form")).toBeInTheDocument();
		const boxes = screen.getAllByLabelText(/digit/i);
		for (let i = 0; i < 6; i++) {
			await fireEvent.input(boxes[i], { target: { value: String(i + 1) } });
		}
		vi.mocked(storeVerify).confirmCode.mockResolvedValueOnce(true);
		const submitBtn = screen.getByRole("button", { name: /verify.*submit/i });
		await fireEvent.click(submitBtn);

		expect(storeNavigation.activeVerifyCode).toBeNull();
		expect(screen.queryByTestId("verify-code-form")).not.toBeInTheDocument();
	});

	it("clears activeVerifyCode when onSkip callback is triggered", async () => {
		vi.mocked(storeAuth).isLoggedIn = true;
		vi.mocked(storeAuth).emailVerified = false;
		storeNavigation.activeVerifyCode = "123456";
		vi.mocked(storeVerify).confirmCode.mockResolvedValueOnce(false);

		render(ProfileAccount);

		expect(screen.getByTestId("verify-code-form")).toBeInTheDocument();

		const skipBtn = screen.getByRole("button", { name: /skip for now/i });
		await fireEvent.click(skipBtn);

		expect(storeNavigation.activeVerifyCode).toBeNull();
		expect(screen.queryByTestId("verify-code-form")).not.toBeInTheDocument();
	});

	it("hides Verify for guests", () => {
		vi.mocked(storeAuth).isLoggedIn = false;
		vi.mocked(storeAuth).emailVerified = false;
		render(ProfileAccount);
		expect(screen.queryByRole("button", { name: /verify/i })).not.toBeInTheDocument();
	});

	it("shows email verified status with a check icon in the tiny font", () => {
		vi.mocked(storeAuth).isLoggedIn = true;
		vi.mocked(storeAuth).emailVerified = true;
		const { container } = render(ProfileAccount);

		const line = screen.getByText("Email verified").closest(".email-status");
		expect(line).toHaveClass("font-tiny");
		expect(container.querySelector(".email-status .pixelart-icons-font-check")).not.toBeNull();
	});

	it("shows no check icon while the email is not verified", () => {
		vi.mocked(storeAuth).isLoggedIn = true;
		vi.mocked(storeAuth).emailVerified = false;
		const { container } = render(ProfileAccount);

		expect(container.querySelector(".email-status .pixelart-icons-font-check")).toBeNull();
	});
});
