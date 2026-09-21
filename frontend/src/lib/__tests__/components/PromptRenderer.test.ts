import { describe, it, expect, beforeEach, afterEach, vi } from "vitest";
import { render, fireEvent } from "@testing-library/svelte";
import PromptRenderer from "$components/game/prompts/PromptRenderer.svelte";
import { storeGame } from "$stores/game.svelte";

type Spy = ReturnType<typeof vi.spyOn>;

describe("PromptRenderer", () => {
	let respondSpy: Spy;

	beforeEach(() => {
		storeGame.activePrompt = null;
		document.body.innerHTML = "";
		respondSpy = vi.spyOn(storeGame, "respondToPrompt").mockImplementation(() => {});
	});

	afterEach(() => {
		respondSpy.mockRestore();
		storeGame.activePrompt = null;
	});

	it("renders four swatches for choose_color and responds with the colour name", async () => {
		storeGame.activePrompt = {
			prompt_id: "p1",
			kind: "choose_color",
			payload: {},
			response_schema: {},
			deadline_ms: 5000
		};

		const { container } = render(PromptRenderer);

		const buttons = container.querySelectorAll<HTMLButtonElement>(".color-button");
		expect(buttons.length).toBe(4);
		expect(buttons[0].textContent?.trim()).toBe("R");

		await fireEvent.click(buttons[0]);
		expect(respondSpy).toHaveBeenCalledWith("p1", "red");
	});

	it("falls back to the schema renderer for an unknown kind and sends the raw enum value", async () => {
		storeGame.activePrompt = {
			prompt_id: "p2",
			kind: "mystery_kind",
			payload: {},
			response_schema: { enum: ["alpha", "beta"] },
			deadline_ms: 5000
		};

		const { container } = render(PromptRenderer);

		const buttons = container.querySelectorAll<HTMLButtonElement>(".enum-button");
		expect(buttons.length).toBe(2);

		await fireEvent.click(buttons[0]);
		expect(respondSpy).toHaveBeenCalledWith("p2", "alpha");
	});

	it("renders a toggle for a boolean response_schema and sends a boolean", async () => {
		storeGame.activePrompt = {
			prompt_id: "p3",
			kind: "mystery_kind",
			payload: {},
			response_schema: { type: "boolean" },
			deadline_ms: 5000
		};

		const { container } = render(PromptRenderer);

		const toggle = container.querySelector<HTMLInputElement>('[role="switch"]');
		expect(toggle).not.toBeNull();

		await fireEvent.click(toggle!);
		expect(respondSpy).toHaveBeenCalledWith("p3", true);
	});

	it("renders nothing without an active prompt", () => {
		const { container } = render(PromptRenderer);

		expect(container.querySelectorAll("button").length).toBe(0);
	});
});
