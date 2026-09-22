import { describe, it, expect, afterEach } from "vitest";
import { render, screen, cleanup } from "@testing-library/svelte";
import { tick } from "svelte";
import ExitConfirmModal from "$components/game/ExitConfirmModal.svelte";

describe("ExitConfirmModal keyboard autofocus", () => {
	afterEach(() => {
		cleanup();
		document.body.innerHTML = "";
	});

	it("focuses the safe 'Stay' action when it opens", async () => {
		render(ExitConfirmModal, {
			props: { open: true, onConfirm: () => {}, onCancel: () => {} }
		});
		await tick();

		expect(document.activeElement).toBe(screen.getByRole("button", { name: "Stay" }));
	});
});
