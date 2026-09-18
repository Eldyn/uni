import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { render, screen, fireEvent, act } from "@testing-library/svelte";
import TooltipStack from "$components/common/TooltipStack.svelte";
import Tooltip from "$components/common/Tooltip.svelte";
import RichText from "$components/common/RichText.svelte";
import { storeTooltipStack } from "$stores/tooltipStack.svelte";

describe("TooltipStack component", () => {
	beforeEach(() => {
		vi.useFakeTimers();
		storeTooltipStack.closeAll();
	});

	afterEach(() => {
		vi.useRealTimers();
		storeTooltipStack.closeAll();
		document.body.innerHTML = "";
	});

	it("renders root tooltip and spawns child tooltip when clicking [k=id] keyword button", async () => {
		render(TooltipStack);

		storeTooltipStack.open("draw");
		expect(storeTooltipStack.stack).toHaveLength(1);

		// "draw" description contains "[k=draw_pile]draw pile[/k]"
		const drawPileBtn = await screen.findByRole("button", { name: "draw pile" });
		expect(drawPileBtn).toBeInTheDocument();

		await fireEvent.click(drawPileBtn);

		expect(storeTooltipStack.stack).toHaveLength(2);
		expect(storeTooltipStack.stack[1].keyword).toBe("draw_pile");

		const childTitle = await screen.findByText("Draw Pile");
		expect(childTitle).toBeInTheDocument();
	});

	it("renders backdrop and popovers at elevated z-index above modal and tooltips", async () => {
		render(TooltipStack);

		storeTooltipStack.open("draw");
		const dialog = await screen.findByRole("dialog");
		expect(dialog).toBeInTheDocument();
		expect(dialog.style.zIndex).toBe("20000");

		const backdrop = document.querySelector(".tooltip-stack-backdrop") as HTMLElement;
		expect(backdrop).toBeInTheDocument();
		expect(getComputedStyle(backdrop).zIndex).toBe("19999");
	});

	it("spawns child tooltip from hover tooltip and handles unmounting gracefully", async () => {
		render(TooltipStack);
		render(Tooltip, {
			props: {
				interactive: true,
				openDelay: 0,
				children: () => {},
				tooltipContent: () => {}
			}
		});

		// Trigger keyword click simulating hover tooltip content
		const dummyBtn = document.createElement("button");
		dummyBtn.getBoundingClientRect = () => ({
			left: 200,
			top: 300,
			width: 60,
			height: 20,
			right: 260,
			bottom: 320,
			x: 200,
			y: 300,
			toJSON: () => {}
		});
		document.body.appendChild(dummyBtn);

		const entry = storeTooltipStack.open("draw_stacking", dummyBtn);
		expect(entry).toBeTruthy();
		expect(entry!.triggerRect.left).toBe(200);

		// Remove element from DOM (simulating hover tooltip close)
		dummyBtn.remove();
		expect(dummyBtn.isConnected).toBe(false);

		// Repositioning must use cached triggerRect rather than zeroing out
		storeTooltipStack.measureAndReposition(entry!.id, { width: 280, height: 120 });
		expect(entry!.x).toBeGreaterThan(0);
		expect(entry!.y).toBeGreaterThan(0);
	});

	it("stacks multiple siblings of the same parent vertically without overlapping", () => {
		// Open parent
		const parent = storeTooltipStack.open("turn", null, null);
		expect(parent).not.toBeNull();

		// Open first child of turn
		const playChild = storeTooltipStack.open("play", null, parent!.id);
		expect(playChild).not.toBeNull();
		expect(playChild!.parentId).toBe(parent!.id);
		expect(playChild!.x).toBeGreaterThan(parent!.x);

		// Open second child of turn (sibling of play)
		const drawChild = storeTooltipStack.open("draw", null, parent!.id);
		expect(drawChild).not.toBeNull();
		expect(drawChild!.parentId).toBe(parent!.id);
		// Same x as play, but stacked vertically below play
		expect(drawChild!.x).toBe(playChild!.x);
		expect(drawChild!.y).toBeGreaterThan(playChild!.y);
	});

	it("renders TooltipCard without close button and closes when card is clicked", async () => {
		storeTooltipStack.open("turn");
		render(TooltipStack);

		expect(screen.queryByRole("button", { name: "✕" })).not.toBeInTheDocument();
		expect(screen.getByText("Turn")).toBeInTheDocument();

		const card = screen.getByText("Turn").closest(".balatro-tooltip-card")!;
		await fireEvent.click(card);
		expect(storeTooltipStack.stack.length).toBe(0);
	});
});
