import { describe, it, expect, vi } from "vitest";
import { render, fireEvent, screen } from "@testing-library/svelte";
import HeldDrawCard from "$components/game/HeldDrawCard.svelte";

function setup(overrides: { isOverDiscard?: (x: number, y: number) => boolean } = {}) {
	const onPlay = vi.fn();
	render(HeldDrawCard, {
		props: {
			cardId: 77,
			leftPercent: 50,
			topPercent: 50,
			onPlay,
			...overrides
		}
	});
	return { onPlay, target: screen.getByTestId("held-draw-hit") };
}

describe("HeldDrawCard", () => {
	it("plays the held card on a click", async () => {
		const { onPlay, target } = setup();
		await fireEvent.pointerDown(target, { button: 0, clientX: 100, clientY: 100 });
		await fireEvent.pointerUp(target, { clientX: 100, clientY: 100 });
		expect(onPlay).toHaveBeenCalledWith(77);
	});

	it("plays the held card when dragged onto the discard pile", async () => {
		const isOverDiscard = vi.fn(() => true);
		const { onPlay, target } = setup({ isOverDiscard });
		await fireEvent.pointerDown(target, { button: 0, clientX: 100, clientY: 100 });
		await fireEvent.pointerMove(target, { clientX: 160, clientY: 180 });
		await fireEvent.pointerUp(target, { clientX: 160, clientY: 180 });
		expect(isOverDiscard).toHaveBeenCalled();
		expect(onPlay).toHaveBeenCalledWith(77);
	});

	it("does not play when a drag ends off the discard pile", async () => {
		const isOverDiscard = vi.fn(() => false);
		const { onPlay, target } = setup({ isOverDiscard });
		await fireEvent.pointerDown(target, { button: 0, clientX: 100, clientY: 100 });
		await fireEvent.pointerMove(target, { clientX: 160, clientY: 180 });
		await fireEvent.pointerUp(target, { clientX: 160, clientY: 180 });
		expect(onPlay).not.toHaveBeenCalled();
	});
});
