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
});
