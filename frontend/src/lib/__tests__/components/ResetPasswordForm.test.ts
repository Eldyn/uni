import { describe, it, expect, vi, beforeEach } from "vitest";
import { render, screen, fireEvent } from "@testing-library/svelte";
import ResetPasswordForm from "$lib/components/auth/ResetPasswordForm.svelte";
import { storeReset } from "$stores/reset.svelte";

vi.mock("$stores/reset.svelte", () => ({
	storeReset: {
		isConfirming: false,
		error: "",
		confirmReset: vi.fn()
	}
}));

vi.mock("$stores/i18n.svelte", () => ({
	storeI18n: {
		locale: "en"
	}
}));

vi.mock("$lib/paraglide/messages.js", () => ({
	reset_title: vi.fn(() => "Set a new password"),
	reset_new_password_label: vi.fn(() => "New password:"),
	reset_confirm_password_label: vi.fn(() => "Confirm new password:"),
	reset_submit: vi.fn(() => "Reset password"),
	auth_login_tab: vi.fn(() => "Log in")
}));

describe("ResetPasswordForm", () => {
	beforeEach(() => {
		vi.clearAllMocks();
		storeReset.isConfirming = false;
		storeReset.error = "";
	});

	it("rejects a too-short password without calling the server", async () => {
		vi.mocked(storeReset.confirmReset).mockResolvedValue(true);
		render(ResetPasswordForm, { props: { token: "tok" } });

		await fireEvent.input(screen.getByLabelText(/^new password:/i), {
			target: { value: "short" }
		});
		await fireEvent.input(screen.getByLabelText(/^confirm new password:/i), {
			target: { value: "short" }
		});
		await fireEvent.click(screen.getByRole("button", { name: /reset password/i }));

		expect(storeReset.confirmReset).not.toHaveBeenCalled();
		expect(screen.getByText(/at least 8 characters/i)).toBeInTheDocument();
	});

	it("rejects mismatched passwords", async () => {
		vi.mocked(storeReset.confirmReset).mockResolvedValue(true);
		render(ResetPasswordForm, { props: { token: "tok" } });

		await fireEvent.input(screen.getByLabelText(/^new password:/i), {
			target: { value: "password123" }
		});
		await fireEvent.input(screen.getByLabelText(/^confirm new password:/i), {
			target: { value: "password456" }
		});
		await fireEvent.click(screen.getByRole("button", { name: /reset password/i }));

		expect(storeReset.confirmReset).not.toHaveBeenCalled();
		expect(screen.getByText(/do not match/i)).toBeInTheDocument();
	});

	it("submits and calls onResetSuccess on success", async () => {
		const onResetSuccess = vi.fn();
		vi.mocked(storeReset.confirmReset).mockResolvedValue(true);
		render(ResetPasswordForm, { props: { token: "tok", onResetSuccess } });

		await fireEvent.input(screen.getByLabelText(/^new password:/i), {
			target: { value: "password123" }
		});
		await fireEvent.input(screen.getByLabelText(/^confirm new password:/i), {
			target: { value: "password123" }
		});
		await fireEvent.click(screen.getByRole("button", { name: /reset password/i }));

		expect(storeReset.confirmReset).toHaveBeenCalledWith("tok", "password123");
		expect(onResetSuccess).toHaveBeenCalledTimes(1);
	});

	it("does not call onResetSuccess when the server rejects", async () => {
		const onResetSuccess = vi.fn();
		vi.mocked(storeReset.confirmReset).mockResolvedValue(false);
		render(ResetPasswordForm, { props: { token: "tok", onResetSuccess } });

		await fireEvent.input(screen.getByLabelText(/^new password:/i), {
			target: { value: "password123" }
		});
		await fireEvent.input(screen.getByLabelText(/^confirm new password:/i), {
			target: { value: "password123" }
		});
		await fireEvent.click(screen.getByRole("button", { name: /reset password/i }));

		expect(onResetSuccess).not.toHaveBeenCalled();
	});
});
