import { describe, it, expect, beforeEach, afterEach, vi } from "vitest";
import { render, fireEvent } from "@testing-library/svelte";
import CardPrompt from "$components/game/prompts/CardPrompt.svelte";
import { storeGame } from "$stores/game.svelte";
import type { PromptOpenPayload } from "$lib/generated/schemas";

type Spy = ReturnType<typeof vi.spyOn>;

describe("CardPrompt", () => {
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

	it("renders integer options as buttons and sends the number back", async () => {
		const prompt: PromptOpenPayload = {
			prompt_id: "choose_card",
			kind: "choose_card",
			payload: { options: [65537, 131074], response_schema: { type: "integer" } },
			response_schema: { type: "integer" },
			deadline_ms: 0
		};

		const { container } = render(CardPrompt, { props: { prompt } });

		const buttons = container.querySelectorAll<HTMLButtonElement>(".card-button");
		expect(buttons.length).toBe(2);

		await fireEvent.click(buttons[1]);
		expect(respondSpy).toHaveBeenCalledWith("choose_card", 131074);
	});
});
