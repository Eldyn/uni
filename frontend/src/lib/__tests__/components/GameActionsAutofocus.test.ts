import { describe, it, expect, beforeEach, vi } from "vitest";
import { render } from "@testing-library/svelte";
import { tick } from "svelte";
import ActionPickColor from "$components/game/actions/ActionPickColor.svelte";
import ActionPlayDrawn from "$components/game/actions/ActionPlayDrawn.svelte";
import ActionChooseTarget from "$components/game/actions/ActionChooseTarget.svelte";
import { storeGame, Action } from "$stores/game.svelte";

describe("GameActions keyboard autofocus", () => {
	beforeEach(() => {
		storeGame.actionRequired = null;
		storeGame.actionContext = null;
		document.body.innerHTML = "";
	});

	it("autofocuses first color button when ChooseType is required", async () => {
		storeGame.actionRequired = Action.ChooseType;
		const { container } = render(ActionPickColor);

		// Wait for requestAnimationFrame to fire focus
		await new Promise((r) => requestAnimationFrame(r));

		const buttons = container.querySelectorAll<HTMLButtonElement>(".color-button");
		expect(buttons.length).toBe(4);
		expect(document.activeElement).toBe(buttons[0]);
		expect(buttons[0].textContent?.trim()).toBe("R");
	});

	it("autofocuses 'Play It' button when PlayDrawn is required", async () => {
		storeGame.actionRequired = Action.PlayDrawn;
		storeGame.actionContext = { id: 42, type: 0, value: 5 };
		const { container } = render(ActionPlayDrawn);

		await new Promise((r) => requestAnimationFrame(r));

		const playBtn = container.querySelector<HTMLButtonElement>(".play-btn");
		expect(playBtn).toBeDefined();
		expect(document.activeElement).toBe(playBtn);
		expect(playBtn?.textContent?.trim()).toBe("Play It");
	});

	it("re-focuses 'Play It' when a new playable card arrives without the prompt unmounting", async () => {
		storeGame.actionRequired = Action.PlayDrawn;
		storeGame.actionContext = { card_id: 42, message: "You drew a playable card!" };
		const { container } = render(ActionPlayDrawn);

		await new Promise((r) => requestAnimationFrame(r));
		const playBtn = container.querySelector<HTMLButtonElement>(".play-btn");
		expect(document.activeElement).toBe(playBtn);

		// The player moves focus to the other choice; a second playable card
		// then arrives. The prompt stays mounted (same action), so only the
		// action's `update` can bring focus back to "Play It".
		const keepBtn = container.querySelector<HTMLButtonElement>(".keep-btn");
		keepBtn?.focus();
		expect(document.activeElement).toBe(keepBtn);

		storeGame.actionContext = { card_id: 43, message: "You drew a playable card!" };
		await tick();
		await new Promise((r) => requestAnimationFrame(r));

		expect(document.activeElement).toBe(playBtn);
	});

	it("autofocuses first target button when ChooseTarget is required", async () => {
		storeGame.actionRequired = Action.ChooseTarget;
		storeGame.actionContext = ["alice", "bob", "charlie"];
		const { container } = render(ActionChooseTarget);

		await new Promise((r) => requestAnimationFrame(r));

		const buttons = container.querySelectorAll<HTMLButtonElement>(".target-button");
		expect(buttons.length).toBe(3);
		expect(document.activeElement).toBe(buttons[0]);
		expect(buttons[0].textContent?.trim()).toBe("alice");
	});
});
