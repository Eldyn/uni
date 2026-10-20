// The colour rows: a region chip per tint region, the palette picker, and the
// swatch grid of the selected palette only. The selected swatch is marked, and
// paid palettes the account does not own are struck but stay selectable. A
// motif shows its skin region as a locked first chip.

import { afterEach, describe, expect, it, vi } from "vitest";
import { cleanup, fireEvent, render, screen } from "@testing-library/svelte";
import ColourRow from "$components/wardrobe/ColourRow.svelte";
import type { Palette } from "$lib/avatar/types";

const PALETTES: Palette[] = [
	{
		id: "basic",
		name: "Basic",
		price: 0,
		starter: true,
		swatches: [
			{ id: "basic:red", hex: "#e84a4a" },
			{ id: "basic:green", hex: "#4aa85a" }
		]
	},
	{
		id: "uni:neon",
		name: "Neon",
		price: 250,
		swatches: [{ id: "uni:neon:hot", hex: "#ff2d95" }]
	}
];
const REGION_LABELS = ["Region 1", "Region 2", "Region 3"];

function renderRow(props: Record<string, unknown> = {}) {
	const onpick = vi.fn();
	const utils = render(ColourRow, {
		props: {
			regions: 2,
			colours: ["basic:red", ""],
			palettes: PALETTES,
			regionLabels: REGION_LABELS,
			locked: new Set(["uni:neon:hot"]),
			label: "Colours",
			onpick,
			...props
		}
	});
	return { onpick, ...utils };
}

afterEach(cleanup);

describe("ColourRow", () => {
	it("renders one labelled row per region", () => {
		const { container } = renderRow();
		expect(container.querySelectorAll(".colour-region")).toHaveLength(2);
		expect(screen.getByText("Region 1")).toBeInTheDocument();
		expect(screen.getByText("Region 2")).toBeInTheDocument();
		expect(screen.queryByText("Region 3")).toBeNull();
	});

	it("marks the selected swatch and locks unowned paid swatches", async () => {
		const { container } = renderRow();
		expect(container.querySelector('[data-swatch-id="basic:red"]')).toHaveAttribute(
			"aria-pressed",
			"true"
		);

		await fireEvent.click(container.querySelector('[data-palette-id="uni:neon"]')!);

		const locked = [
			...container.querySelectorAll<HTMLButtonElement>('.colour-swatch[data-locked="true"]')
		];
		expect(locked.length).toBeGreaterThan(0);
		for (const swatch of locked) {
			expect(swatch).toHaveAttribute("aria-disabled", "true");
			expect(swatch.dataset.swatchId).toBe("uni:neon:hot");
		}
	});

	it("shows a slash and a buy prompt, and no lock icon, on a locked swatch", async () => {
		const { container } = renderRow({
			locked: new Set(["uni:neon:hot"]),
			lockedLabel: "Buy this color in the Shop"
		});
		await fireEvent.click(container.querySelector('[data-palette-id="uni:neon"]')!);

		const locked = container.querySelector<HTMLButtonElement>('[data-swatch-id="uni:neon:hot"]');
		expect(locked).toHaveAttribute("title", "Buy this color in the Shop");
		expect(locked?.querySelector(".pixelart-icons-font-lock")).toBeNull();
	});

	it("asks to buy when a locked swatch is clicked", async () => {
		const onlocked = vi.fn();
		const { container } = renderRow({
			locked: new Set(["uni:neon:hot"]),
			onlocked
		});
		await fireEvent.click(container.querySelector('[data-palette-id="uni:neon"]')!);

		await fireEvent.click(container.querySelector('[data-swatch-id="uni:neon:hot"]')!);
		expect(onlocked).toHaveBeenCalledTimes(1);
	});

	it("picks a swatch for the row's region", async () => {
		const { container, onpick } = renderRow();
		await fireEvent.click(container.querySelector('[data-region="1"]')!);
		await fireEvent.click(container.querySelector('[data-swatch-id="basic:green"]')!);
		expect(onpick).toHaveBeenCalledWith(1, "basic:green");
	});

	it("shows a checkerboard reset tile for every palette", async () => {
		const { container } = renderRow();
		const reset = container.querySelector<HTMLElement>('[data-swatch-id="basic:none"]');

		expect(reset).not.toBeNull();
		expect(reset?.classList.contains("colour-swatch--none")).toBe(true);
		expect(reset).toHaveAttribute("aria-label", "None");

		await fireEvent.click(container.querySelector('[data-palette-id="uni:neon"]')!);
		expect(container.querySelector('[data-swatch-id="basic:none"]')).not.toBeNull();
	});

	it("hides the reset tile when reset is not allowed", async () => {
		const { container } = renderRow({ allowReset: false });

		expect(container.querySelector('[data-swatch-id="basic:none"]')).toBeNull();

		await fireEvent.click(container.querySelector('[data-palette-id="uni:neon"]')!);
		expect(container.querySelector('[data-swatch-id="basic:none"]')).toBeNull();
	});

	it("shows the reset tile by default", () => {
		const { container } = renderRow();

		expect(container.querySelector('[data-swatch-id="basic:none"]')).not.toBeNull();
	});

	it("picks the reset swatch for the active region", async () => {
		const { container, onpick } = renderRow();

		await fireEvent.click(container.querySelector('[data-region="1"]')!);
		await fireEvent.click(container.querySelector('[data-swatch-id="basic:none"]')!);

		expect(onpick).toHaveBeenCalledWith(1, "basic:none");
	});

	it("marks the reset tile when the region stores the reset swatch", () => {
		const { container } = renderRow({ colours: ["basic:none", ""] });

		expect(container.querySelector('[data-swatch-id="basic:none"]')).toHaveAttribute(
			"aria-pressed",
			"true"
		);
	});

	it("shows the checkerboard chip for a region that stores the reset swatch", () => {
		const { container } = renderRow({ colours: ["basic:none", ""] });

		expect(container.querySelector('[data-region="0"] .colour-chip')).toHaveClass(
			"colour-chip--none"
		);
	});

	it("shows only the swatches of the palette of the active region's color", () => {
		const { container } = renderRow({ colours: ["uni:neon:hot", ""], locked: new Set() });
		expect(container.querySelector('[data-swatch-id="uni:neon:hot"]')).not.toBeNull();
		expect(container.querySelector('[data-swatch-id="basic:red"]')).toBeNull();
	});

	it("opens the first palette that is not locked when the region has no color", async () => {
		const { container } = renderRow();
		await fireEvent.click(container.querySelector('[data-region="1"]')!);
		expect(container.querySelector(".colour-swatches")?.getAttribute("data-palette")).toBe("basic");
	});

	it("strikes a locked palette tile without a lock icon", () => {
		const { container } = renderRow();
		const tile = container.querySelector<HTMLElement>('[data-palette-id="uni:neon"]');
		expect(tile?.dataset.locked).toBe("true");
		expect(tile?.querySelector(".pixelart-icons-font-lock")).toBeNull();
		expect(tile?.querySelectorAll(".palette-cell")).toHaveLength(1);
	});

	it("shows the skin as a locked first row on a motif and labels the rest from region 2", () => {
		const { container } = renderRow({ regions: 3, skinFirst: true, skinHex: "#123456" });
		expect(container.querySelector('[data-region="skin"]')).toBeTruthy();
		expect(container.querySelectorAll('.colour-region:not([data-region="skin"])')).toHaveLength(2);
		expect(screen.getByText("Region 2")).toBeInTheDocument();
		expect(screen.getByText("Region 3")).toBeInTheDocument();
	});

	it("renders six rows with labels 1 to 6 for a six-region item", () => {
		const labels = [1, 2, 3, 4, 5, 6].map((n) => `Region ${n}`);
		const { container } = renderRow({ regions: 6, colours: [], regionLabels: labels });

		expect(container.querySelectorAll(".colour-region")).toHaveLength(6);
		expect(screen.getByText("Region 6")).toBeInTheDocument();
	});

	it("falls back to Region N when no label is given", () => {
		renderRow({ regions: 5, colours: [], regionLabels: [] });

		expect(screen.getByText("Region 5")).toBeInTheDocument();
	});

	it("renders no row for zero regions", () => {
		const { container } = renderRow({ regions: 0, colours: [] });

		expect(container.querySelectorAll(".colour-region")).toHaveLength(0);
	});

	it("a motif with six regions shows skin plus five pickable rows", () => {
		const labels = [1, 2, 3, 4, 5, 6].map((n) => `Region ${n}`);
		const { container } = renderRow({
			regions: 6,
			colours: [],
			regionLabels: labels,
			skinFirst: true,
			skinHex: "#123456"
		});

		expect(container.querySelectorAll('.colour-region:not([data-region="skin"])')).toHaveLength(5);
		expect(screen.getByText("Region 6")).toBeInTheDocument();
	});

	it("labels a motif's locked skin row with the skin label when one is given", () => {
		const { container } = renderRow({
			regions: 3,
			skinFirst: true,
			skinHex: "#123456",
			skinLabel: "Skin"
		});

		const skinRow = container.querySelector('[data-region="skin"] .colour-row-label');
		expect(skinRow).toHaveTextContent("Skin");
		expect(screen.queryByText("Region 1")).toBeNull();
	});
});
