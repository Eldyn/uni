import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { render, screen, fireEvent, act } from "@testing-library/svelte";
import EnumSelector from "$components/lobby/settings/EnumSelector.svelte";
import { storeTooltipStack } from "$stores/tooltipStack.svelte";

const options = [
	{ value: "standard", label: "Standard", description: "The standard way to win." },
	{ value: "elimination", label: "Elimination", description: "Knock players out." }
];

function baseProps(overrides: Record<string, unknown> = {}) {
	return {
		label: "Game Mode",
		description: "Choose how a match is won.",
		value: "standard",
		options,
		oncommit: vi.fn(),
		...overrides
	};
}

describe("EnumSelector field and per-selection tooltips", () => {
	beforeEach(() => {
		vi.useFakeTimers();
	});

	afterEach(() => {
		vi.useRealTimers();
		storeTooltipStack.closeAll();
		document.body.innerHTML = "";
	});

	it("shows the field description behind the info icon", async () => {
		render(EnumSelector, { props: baseProps() });

		const info = screen.getByRole("button", { name: "More info" });
		await fireEvent.mouseEnter(info.closest(".tooltip-container")!);
		await act(() => vi.runAllTimers());

		const tooltip = screen.getByRole("tooltip");
		expect(tooltip.querySelector(".tooltip-title")).toHaveTextContent("Game Mode");
		expect(tooltip).toHaveTextContent("Choose how a match is won.");
	});

	it("shows the current option description on the select", async () => {
		render(EnumSelector, { props: baseProps() });

		const trigger = screen.getByRole("button", { name: /Standard/ });
		await fireEvent.mouseEnter(trigger.closest(".tooltip-container")!);
		await act(() => vi.runAllTimers());

		const tooltip = screen.getByRole("tooltip");
		expect(tooltip.querySelector(".tooltip-title")).toHaveTextContent("Standard");
		expect(tooltip).toHaveTextContent("The standard way to win.");
	});

	it("updates the select tooltip when the selection changes", async () => {
		const { rerender } = render(EnumSelector, { props: baseProps() });

		await rerender(baseProps({ value: "elimination" }));

		const trigger = screen.getByRole("button", { name: /Elimination/ });
		await fireEvent.mouseEnter(trigger.closest(".tooltip-container")!);
		await act(() => vi.runAllTimers());

		const tooltip = screen.getByRole("tooltip");
		expect(tooltip.querySelector(".tooltip-title")).toHaveTextContent("Elimination");
		expect(tooltip).toHaveTextContent("Knock players out.");
	});

	it("renders no info icon when the field has no description", () => {
		render(EnumSelector, { props: baseProps({ description: undefined }) });

		expect(screen.queryByRole("button", { name: "More info" })).not.toBeInTheDocument();
	});
});
