import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { render, screen, fireEvent, act } from "@testing-library/svelte";
import TooltipStack from "$components/common/TooltipStack.svelte";
import TwoKeywordHarness from "./TwoKeywordHarness.svelte";
import { storeTooltipStack } from "$stores/tooltipStack.svelte";

describe("root tooltip with two keywords", () => {
	beforeEach(() => {
		vi.useFakeTimers();
		storeTooltipStack.closeAll();
	});

	afterEach(() => {
		vi.useRealTimers();
		storeTooltipStack.closeAll();
		document.body.innerHTML = "";
	});

	it("opens two sibling children from the root popover", async () => {
		render(TooltipStack);
		render(TwoKeywordHarness, { props: { openDelay: 0, interactive: true } });

		const trigger = screen.getByRole("button", { name: "Trigger Button" });
		await fireEvent.mouseEnter(trigger.parentElement!);
		await act(() => vi.runAllTimers());

		// Click "play" keyword inside root popover
		const playBtn = screen.getByRole("button", { name: "play" });
		await fireEvent.click(playBtn);
		await act(() => vi.runAllTimers());

		expect(storeTooltipStack.stack).toHaveLength(1);
		expect(storeTooltipStack.stack[0].keyword).toBe("play");

		// Click "draw" keyword inside the SAME root popover
		const drawBtn = screen.getByRole("button", { name: "draw" });
		await fireEvent.click(drawBtn);
		await act(() => vi.runAllTimers());

		// BOTH children should be open as siblings, stacked apart (not overlapping).
		const keywords = storeTooltipStack.stack.map((s) => s.keyword);
		expect(keywords).toEqual(["play", "draw"]);
		const play = storeTooltipStack.stack[0];
		const draw = storeTooltipStack.stack[1];
		expect(draw.depth).toBe(0);
		expect(play.depth).toBe(0);
		expect(draw.x).toBe(play.x);
		expect(draw.y).not.toBe(play.y);
	});
});
