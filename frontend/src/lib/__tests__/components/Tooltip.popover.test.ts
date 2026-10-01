import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { render, screen, fireEvent, act } from "@testing-library/svelte";
import { createRawSnippet } from "svelte";
import TooltipHarness from "./TooltipHarness.svelte";
import Tooltip from "$components/common/Tooltip.svelte";
import { storeTooltipStack } from "$stores/tooltipStack.svelte";

describe("Tooltip static pixel popover", () => {
	beforeEach(() => {
		vi.useFakeTimers();
		storeTooltipStack.closeAll();
	});

	afterEach(() => {
		vi.useRealTimers();
		document.body.innerHTML = "";
		storeTooltipStack.closeAll();
	});

	it("opens on hover after delay, renders pixel-bordered tooltip with role", async () => {
		render(TooltipHarness, { props: { openDelay: 100 } });
		const trigger = screen.getByRole("button", { name: "Trigger Button" });

		// Mouse enter starts timer
		await fireEvent.mouseEnter(trigger.parentElement!);
		expect(screen.queryByRole("tooltip")).not.toBeInTheDocument();

		// Advance timer past openDelay
		await act(() => {
			vi.advanceTimersByTime(150);
		});

		const tooltip = screen.getByRole("tooltip");
		expect(tooltip).toBeInTheDocument();
		expect(tooltip).toHaveTextContent("Popover description text");
		expect(tooltip).toHaveClass("pixel-popover-tooltip");
		expect(tooltip.querySelector(".pixel-bordered")).toBeInTheDocument();

		// Mouse leave closes tooltip
		await fireEvent.mouseLeave(trigger.parentElement!);
		expect(screen.queryByRole("tooltip")).not.toBeInTheDocument();
	});

	it("opens on keyboard focus immediately and sets aria-describedby", async () => {
		render(TooltipHarness, { props: { openDelay: 100 } });
		const trigger = screen.getByRole("button", { name: "Trigger Button" });

		expect(trigger).not.toHaveAttribute("aria-describedby");

		// Focus trigger
		await fireEvent.focusIn(trigger.parentElement!);
		const tooltip = screen.getByRole("tooltip");
		expect(tooltip).toBeInTheDocument();

		const tooltipId = tooltip.getAttribute("id");
		expect(tooltipId).toBeTruthy();
		expect(trigger).toHaveAttribute("aria-describedby", tooltipId!);

		// Blur trigger
		await fireEvent.focusOut(trigger.parentElement!);
		expect(screen.queryByRole("tooltip")).not.toBeInTheDocument();
		expect(trigger).not.toHaveAttribute("aria-describedby");
	});

	it("stays pinned to static coordinates when mouse moves within trigger", async () => {
		render(TooltipHarness, { props: { openDelay: 0 } });
		const trigger = screen.getByRole("button", { name: "Trigger Button" });

		await fireEvent.mouseEnter(trigger.parentElement!);
		await act(() => vi.runAllTimers());

		const tooltip = screen.getByRole("tooltip");
		const initialLeft = tooltip.style.left;
		const initialTop = tooltip.style.top;

		// Move mouse around inside trigger
		await fireEvent.mouseMove(trigger.parentElement!, { clientX: 500, clientY: 400 });
		expect(tooltip.style.left).toBe(initialLeft);
		expect(tooltip.style.top).toBe(initialTop);

		await fireEvent.mouseMove(trigger.parentElement!, { clientX: 800, clientY: 700 });
		expect(tooltip.style.left).toBe(initialLeft);
		expect(tooltip.style.top).toBe(initialTop);
	});

	it("supports interactive hover bridge into tooltip content", async () => {
		render(TooltipHarness, { props: { openDelay: 0, closeDelay: 150, interactive: true } });
		const trigger = screen.getByRole("button", { name: "Trigger Button" });

		await fireEvent.mouseEnter(trigger.parentElement!);
		await act(() => vi.runAllTimers());

		const tooltip = screen.getByRole("tooltip");
		expect(tooltip).toBeInTheDocument();
		expect(tooltip).toHaveClass("interactive");

		// Mouse leaves trigger - closeDelay begins
		await fireEvent.mouseLeave(trigger.parentElement!);
		// Advance half the close delay (75ms): still open
		await act(() => vi.advanceTimersByTime(75));
		expect(screen.getByRole("tooltip")).toBeInTheDocument();

		// Pointer enters tooltip: cancels close timer
		await fireEvent.mouseEnter(tooltip);
		await act(() => vi.advanceTimersByTime(200));
		expect(screen.getByRole("tooltip")).toBeInTheDocument();

		// Pointer leaves tooltip: closes after closeDelay
		await fireEvent.mouseLeave(tooltip);
		await act(() => vi.advanceTimersByTime(150));
		expect(screen.queryByRole("tooltip")).not.toBeInTheDocument();
	});

	it("does NOT close root tooltip when stack has items", async () => {
		render(TooltipHarness, {
			props: { openDelay: 0, interactive: true }
		});

		const trigger = screen.getByRole("button", { name: "Trigger Button" });
		await fireEvent.mouseEnter(trigger.parentElement!);
		await act(() => vi.runAllTimers());

		expect(screen.getByRole("tooltip")).toBeInTheDocument();

		// Simulate a child item opened in storeTooltipStack (e.g. glossary keyword click)
		storeTooltipStack.open("vanilla:turn");
		await act(() => vi.runAllTimers());

		// Root tooltip must remain open!
		expect(screen.getByRole("tooltip")).toBeInTheDocument();
	});

	it("does not close root tooltip when the pointer leaves it for a portaled child glossary tooltip, and closes once the child tree empties with the pointer away", async () => {
		render(TooltipHarness, {
			props: { openDelay: 0, closeDelay: 150, interactive: true }
		});

		const trigger = screen.getByRole("button", { name: "Trigger Button" });
		await fireEvent.mouseEnter(trigger.parentElement!);
		await act(() => vi.runAllTimers());

		const tooltip = screen.getByRole("tooltip");
		await fireEvent.mouseEnter(tooltip);

		// A glossary keyword inside the tooltip content spawns a child tooltip,
		// portaled elsewhere in the DOM by TooltipStack.
		await act(() => storeTooltipStack.open("vanilla:turn"));

		// The pointer leaves this tooltip's own DOM to move onto the (separately
		// portaled) child — its mouseleave fires even though the user never
		// really left the tooltip tree.
		await fireEvent.mouseLeave(tooltip);
		await act(() => vi.advanceTimersByTime(300));
		expect(screen.getByRole("tooltip")).toBeInTheDocument();

		// Once the child tree closes with the pointer no longer over the root
		// tooltip, the root should close too.
		await act(() => storeTooltipStack.closeAll());
		await act(() => vi.advanceTimersByTime(150));
		expect(screen.queryByRole("tooltip")).not.toBeInTheDocument();
	});

	it("renders title in root tooltip", async () => {
		const { container } = render(Tooltip, {
			props: {
				title: "7-0 Rule",
				openDelay: 0,
				children: createRawSnippet(() => ({
					render: () => `<button type="button">Trigger</button>`
				})),
				tooltipContent: createRawSnippet(() => ({
					render: () => `<span>Body text</span>`
				}))
			}
		});

		const trigger = container.querySelector(".tooltip-container")!;
		await fireEvent.mouseEnter(trigger);
		await act(() => vi.runAllTimers());

		const tooltip = screen.getByRole("tooltip");
		expect(tooltip).toBeInTheDocument();
		expect(tooltip).toHaveTextContent("7-0 Rule");
	});
});
