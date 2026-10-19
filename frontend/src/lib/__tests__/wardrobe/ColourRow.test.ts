// The colour rows: one labelled row per tint region, wrapping 28px swatch
// buttons, the selected swatch marked, and paid swatches the account does not
// own locked. A motif shows its skin region as a locked first row.

import { afterEach, describe, expect, it, vi } from "vitest";
import { cleanup, fireEvent, render, screen } from "@testing-library/svelte";
import ColourRow from "$components/wardrobe/ColourRow.svelte";
import type { Swatch } from "$lib/avatar/types";

const SWATCHES: Swatch[] = [
	{ id: "basic:red", hex: "#e84a4a" },
	{ id: "basic:green", hex: "#4aa85a" },
	{ id: "uni:neon:hot", hex: "#ff2d95" }
];
const REGION_LABELS = ["Region 1", "Region 2", "Region 3"];

function renderRow(props: Record<string, unknown> = {}) {
	const onpick = vi.fn();
	const utils = render(ColourRow, {
		props: {
			regions: 2,
			colours: ["basic:red", ""],
			swatches: SWATCHES,
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
		expect(container.querySelectorAll(".colour-row")).toHaveLength(2);
		expect(screen.getByText("Region 1")).toBeInTheDocument();
		expect(screen.getByText("Region 2")).toBeInTheDocument();
		expect(screen.queryByText("Region 3")).toBeNull();
	});

	it("marks the selected swatch and locks unowned paid swatches", () => {
		const { container } = renderRow();
		expect(container.querySelector('[data-swatch-id="basic:red"]')).toHaveAttribute(
			"aria-pressed",
			"true"
		);

		const locked = [...container.querySelectorAll<HTMLButtonElement>('[data-locked="true"]')];
		expect(locked.length).toBeGreaterThan(0);
		for (const swatch of locked) {
			expect(swatch).toHaveAttribute("aria-disabled", "true");
			expect(swatch.dataset.swatchId).toBe("uni:neon:hot");
		}
	});

	it("shows a slash and a buy prompt, and no lock icon, on a locked swatch", () => {
		const { container } = renderRow({
			locked: new Set(["uni:neon:hot"]),
			lockedLabel: "Buy this color in the Shop"
		});

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

		await fireEvent.click(container.querySelector('[data-swatch-id="uni:neon:hot"]')!);
		expect(onlocked).toHaveBeenCalledTimes(1);
	});

	it("picks a swatch for the row's region", async () => {
		const { container, onpick } = renderRow();
		const row = container.querySelector('[data-region="1"]');
		await fireEvent.click(row!.querySelector('[data-swatch-id="basic:green"]')!);
		expect(onpick).toHaveBeenCalledWith(1, "basic:green");
	});

	it("shows the skin as a locked first row on a motif and labels the rest from region 2", () => {
		const { container } = renderRow({ regions: 3, skinFirst: true, skinHex: "#123456" });
		expect(container.querySelector('[data-region="skin"]')).toBeTruthy();
		expect(container.querySelectorAll('.colour-row:not([data-region="skin"])')).toHaveLength(2);
		expect(screen.getByText("Region 2")).toBeInTheDocument();
		expect(screen.getByText("Region 3")).toBeInTheDocument();
	});

	it("renders six rows with labels 1 to 6 for a six-region item", () => {
		const labels = [1, 2, 3, 4, 5, 6].map((n) => `Region ${n}`);
		const { container } = renderRow({ regions: 6, colours: [], regionLabels: labels });

		expect(container.querySelectorAll(".colour-row")).toHaveLength(6);
		expect(screen.getByText("Region 6")).toBeInTheDocument();
	});

	it("falls back to Region N when no label is given", () => {
		renderRow({ regions: 5, colours: [], regionLabels: [] });

		expect(screen.getByText("Region 5")).toBeInTheDocument();
	});

	it("renders no row for zero regions", () => {
		const { container } = renderRow({ regions: 0, colours: [] });

		expect(container.querySelectorAll(".colour-row")).toHaveLength(0);
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

		expect(container.querySelectorAll('.colour-row:not([data-region="skin"])')).toHaveLength(5);
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
