import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { render, screen, fireEvent, act } from "@testing-library/svelte";
import Toggle from "$components/lobby/settings/Toggle.svelte";
import RulesGrid from "$components/lobby/settings/RulesGrid.svelte";
import { storeTooltipStack } from "$stores/tooltipStack.svelte";

describe("Lobby settings interactive tooltips and rich text", () => {
	beforeEach(() => {
		vi.useFakeTimers();
	});

	afterEach(() => {
		vi.useRealTimers();
		storeTooltipStack.closeAll();
		document.body.innerHTML = "";
	});

	it("renders keyword link in Toggle tooltip", async () => {
		render(Toggle, {
			props: {
				label: "Test Toggle",
				description: "Effect allows player to [k=draw]draw[/k] cards.",
				checked: false,
				oncommit: vi.fn()
			}
		});

		const toggleLabel = screen.getByText("Test Toggle");
		const container = toggleLabel.closest(".tooltip-container")!;
		await fireEvent.mouseEnter(container);
		await act(() => vi.runAllTimers());

		const tooltip = screen.getByRole("tooltip");
		expect(tooltip).toBeInTheDocument();
		expect(tooltip).toHaveClass("interactive");

		const keywordBtn = tooltip.querySelector("button.glossary-keyword-btn");
		expect(keywordBtn).toBeInTheDocument();
		expect(keywordBtn).toHaveTextContent("draw");
	});

	it("renders keyword link in RulesGrid tooltip", async () => {
		render(RulesGrid, {
			props: {
				rules: [
					{
						id: "seven_zero",
						label: "7-0 Rule",
						description: "Swap hands or rotate in [k=turn]turn[/k] direction.",
						enabled: true
					}
				],
				onrulechange: vi.fn()
			}
		});

		const ruleLabel = screen.getByText("7-0 Rule");
		const container = ruleLabel.closest(".tooltip-container")!;
		await fireEvent.mouseEnter(container);
		await act(() => vi.runAllTimers());

		const tooltip = screen.getByRole("tooltip");
		expect(tooltip).toBeInTheDocument();
		expect(tooltip).toHaveClass("interactive");

		const keywordBtn = tooltip.querySelector("button.glossary-keyword-btn");
		expect(keywordBtn).toBeInTheDocument();
		expect(keywordBtn).toHaveTextContent("turn");
	});

	it("clicking keyword in RulesGrid tooltip spawns glossary tooltip in storeTooltipStack", async () => {
		render(RulesGrid, {
			props: {
				rules: [
					{
						id: "draw_stacking",
						label: "Draw Stacking",
						description: "Stack +2 or +4 cards to pass the accumulated [k=draw]draw[/k] penalty.",
						enabled: true
					}
				],
				onrulechange: vi.fn()
			}
		});

		const ruleLabel = screen.getByText("Draw Stacking");
		const container = ruleLabel.closest(".tooltip-container")!;
		await fireEvent.mouseEnter(container);
		await act(() => vi.runAllTimers());

		const tooltip = screen.getByRole("tooltip");
		const keywordBtn = tooltip.querySelector<HTMLButtonElement>("button.glossary-keyword-btn")!;
		expect(keywordBtn).toBeInTheDocument();

		await fireEvent.click(keywordBtn);

		expect(storeTooltipStack.stack).toHaveLength(1);
		expect(storeTooltipStack.stack[0].keyword).toBe("draw");
		expect(storeTooltipStack.stack[0].title).toBe("Draw");
	});

	it("renders the setting label as the tooltip title in Toggle", async () => {
		render(Toggle, {
			props: {
				label: "Test Toggle",
				description: "Effect allows player to [k=draw]draw[/k] cards.",
				checked: false,
				oncommit: vi.fn()
			}
		});

		const toggleLabel = screen.getByText("Test Toggle");
		const container = toggleLabel.closest(".tooltip-container")!;
		await fireEvent.mouseEnter(container);
		await act(() => vi.runAllTimers());

		const tooltip = screen.getByRole("tooltip");
		expect(tooltip.querySelector(".tooltip-title")).toHaveTextContent("Test Toggle");
	});

	it("renders the rule label as title and a Rule tag in RulesGrid", async () => {
		render(RulesGrid, {
			props: {
				rules: [
					{
						id: "seven_zero",
						label: "7-0 Rule",
						description: "Swap hands or rotate in [k=turn]turn[/k] direction.",
						enabled: true
					}
				],
				onrulechange: vi.fn()
			}
		});

		const ruleLabel = screen.getByText("7-0 Rule");
		const container = ruleLabel.closest(".tooltip-container")!;
		await fireEvent.mouseEnter(container);
		await act(() => vi.runAllTimers());

		const tooltip = screen.getByRole("tooltip");
		expect(tooltip.querySelector(".tooltip-title")).toHaveTextContent("7-0 Rule");
		expect(tooltip.querySelector(".tooltip-tag-pill")).toBeInTheDocument();
	});
});
