import { describe, it, expect } from "vitest";
import { render, screen, fireEvent } from "@testing-library/svelte";

import CardsPlayedChart from "$components/profile/CardsPlayedChart.svelte";
import type { CardsPlayedGroup } from "$components/profile/profileModel";
import chartSource from "../../../components/profile/CardsPlayedChart.svelte?raw";

const coloursGroup: CardsPlayedGroup = {
	id: "colours",
	rows: [
		{ key: "red", count: 41, ratio: 1 },
		{ key: "blue", count: 33, ratio: 0.8 },
		{ key: "green", count: 25, ratio: 0.61 },
		{ key: "yellow", count: 20, ratio: 0.49 }
	]
};

const valuesGroup: CardsPlayedGroup = {
	id: "values",
	rows: [
		{ key: "1", count: 9, ratio: 1 },
		{ key: "5", count: 4, ratio: 0.44 },
		{ key: "0", count: 2, ratio: 0.22 }
	]
};

const actionsGroup: CardsPlayedGroup = {
	id: "actions",
	rows: [
		{ key: "draw4", count: 31, ratio: 1 },
		{ key: "skip", count: 18, ratio: 0.58 },
		{ key: "draw2", count: 9, ratio: 0.29 },
		{ key: "jolly", count: 7, ratio: 0.23 }
	]
};

const allGroups = () => [coloursGroup, valuesGroup, actionsGroup];

/** The declarations of one CSS rule, read straight from the component source. */
function cssBlock(source: string, selector: string): string {
	const escaped = selector.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
	const match = source.match(new RegExp(`${escaped}\\s*\\{([^}]*)\\}`));
	if (!match) throw new Error(`rule not found: ${selector}`);
	return match[1];
}

function captionText(container: HTMLElement): string {
	return container.querySelector(".podium-caption")?.textContent?.replace(/\s+/g, " ").trim() ?? "";
}

describe("CardsPlayedChart", () => {
	it("starts on the first group and shows its bars", () => {
		render(CardsPlayedChart, { props: { groups: allGroups() } });

		expect(screen.getByRole("tab", { name: "Colors" })).toHaveAttribute("aria-selected", "true");
		expect(screen.getByText("Red")).toBeInTheDocument();
		expect(screen.getByText("41")).toBeInTheDocument();
		expect(screen.queryByText("1")).not.toBeInTheDocument();
	});

	it("switching tabs shows the other group", async () => {
		render(CardsPlayedChart, { props: { groups: allGroups() } });

		await fireEvent.click(screen.getByRole("tab", { name: "Values" }));

		expect(screen.getByRole("tab", { name: "Values" })).toHaveAttribute("aria-selected", "true");
		expect(screen.getByText("1")).toBeInTheDocument();
		expect(screen.getByText("9")).toBeInTheDocument();
		expect(screen.queryByText("Red")).not.toBeInTheDocument();
	});

	it("arrow keys move between tabs", async () => {
		render(CardsPlayedChart, { props: { groups: allGroups() } });

		await fireEvent.keyDown(screen.getByRole("tablist"), { key: "ArrowRight" });

		const valuesTab = screen.getByRole("tab", { name: "Values" });
		expect(valuesTab).toHaveAttribute("aria-selected", "true");
		expect(valuesTab).toHaveFocus();
		expect(screen.getByText("1")).toBeInTheDocument();
		expect(screen.queryByText("Red")).not.toBeInTheDocument();
	});

	it("bars show counts as text and labels below", () => {
		const { container } = render(CardsPlayedChart, {
			props: { groups: [coloursGroup] }
		});

		expect(screen.getByText("41")).toBeInTheDocument();
		expect(screen.getByText("Red")).toBeInTheDocument();
		const bar = container.querySelector(".bar");
		expect(bar).toHaveAttribute("aria-hidden", "true");
	});

	it("renders only the most played action card", () => {
		const { container } = render(CardsPlayedChart, { props: { groups: [actionsGroup] } });

		const cards = container.querySelectorAll(".podium-card");
		expect(cards).toHaveLength(1);
		expect(cards[0].querySelector(".podium-face")?.textContent?.trim()).toBe("+4");
		expect(cards[0].querySelector(".podium-count")?.textContent?.trim()).toBe("31");
	});

	it("keeps model order on a tie for the top card", () => {
		const group: CardsPlayedGroup = {
			id: "actions",
			rows: [
				{ key: "draw4", count: 14, ratio: 1 },
				{ key: "skip", count: 14, ratio: 1 },
				{ key: "draw2", count: 3, ratio: 0.21 }
			]
		};
		const { container } = render(CardsPlayedChart, { props: { groups: [group] } });

		expect(container.querySelectorAll(".podium-card")).toHaveLength(1);
		expect(container.querySelector(".podium-face")?.textContent?.trim()).toBe("+4");
	});

	it("renders the card as a plain, non-interactive element", () => {
		const { container } = render(CardsPlayedChart, { props: { groups: [actionsGroup] } });

		const card = container.querySelector(".podium-card") as HTMLElement;
		expect(card.tagName).toBe("DIV");
		expect(card).not.toHaveAttribute("role");
		expect(card).not.toHaveAttribute("aria-pressed");
		expect(card).not.toHaveAttribute("onclick");
		expect(card).not.toHaveAttribute("onkeydown");
		expect(card).not.toHaveClass("pixel-bordered");
		expect(screen.queryByRole("button", { name: /\+4/ })).toBeNull();

		const cardRule = cssBlock(chartSource, ".podium-card");
		expect(cardRule).not.toMatch(/cursor:\s*pointer/);
		expect(cardRule).not.toMatch(/--pc-fill/);
		expect(cardRule).not.toMatch(/transform:\s*scale/);
	});

	it("centres the single card without sideways scrolling", () => {
		const podium = cssBlock(chartSource, ".podium");
		expect(podium).toMatch(/justify-content:\s*center/);
		expect(podium).not.toMatch(/overflow-x/);
		expect(podium).not.toMatch(/align-items:\s*flex-end/);
	});

	it("draws the card art larger", () => {
		const art = cssBlock(chartSource, ".podium-art");
		const width = art.match(/--loader-card-width:\s*([\d.]+)rem/);
		expect(width).not.toBeNull();
		expect(Number(width?.[1])).toBeGreaterThanOrEqual(4.5);
	});

	it("sets the label, the count and the caption in the tiny font", () => {
		const { container } = render(CardsPlayedChart, { props: { groups: [actionsGroup] } });

		const card = container.querySelector(".podium-card") as HTMLElement;
		expect(card.querySelector(".podium-face")).toHaveClass("font-tiny");
		expect(card.querySelector(".podium-count")).toHaveClass("font-tiny");
		expect(container.querySelector(".podium-caption")).toHaveClass("font-tiny");

		const order = [...card.querySelectorAll(".podium-art, .podium-face, .podium-count")].map(
			(element) =>
				element.classList.contains("podium-art")
					? "art"
					: element.classList.contains("podium-face")
						? "face"
						: "count"
		);
		expect(order).toEqual(["art", "face", "count"]);
	});

	it("shakes the percentage in the caption", () => {
		const { container } = render(CardsPlayedChart, { props: { groups: [actionsGroup] } });

		const shake = container.querySelector(".podium-caption .shake");
		expect(shake?.textContent?.trim()).toBe("174%");
		expect(cssBlock(chartSource, ".shake")).toMatch(/animation:/);
		expect(chartSource).toMatch(/@keyframes\s+shake/);
		expect(chartSource).toMatch(/translateX\(\s*-0\.5px\s*\)/);
		expect(chartSource).toMatch(/translateX\(\s*0\.5px\s*\)/);
		expect(chartSource).toMatch(
			/@media\s*\(prefers-reduced-motion:\s*reduce\)\s*\{[^@]*\.shake[^}]*animation:\s*none/
		);
	});

	it("caption says more with a positive percent", () => {
		const group: CardsPlayedGroup = {
			id: "actions",
			rows: [
				{ key: "draw4", count: 14, ratio: 1 },
				{ key: "skip", count: 10, ratio: 0.71 },
				{ key: "draw2", count: 10, ratio: 0.71 }
			]
		};
		const { container } = render(CardsPlayedChart, { props: { groups: [group] } });

		expect(captionText(container)).toMatch(/40% more/);
		expect(container.querySelector(".podium-caption")).toHaveAttribute("aria-live", "polite");
	});

	it("caption says same at 0 percent", () => {
		const group: CardsPlayedGroup = {
			id: "actions",
			rows: [
				{ key: "draw4", count: 10, ratio: 1 },
				{ key: "skip", count: 10, ratio: 1 },
				{ key: "draw2", count: 10, ratio: 1 }
			]
		};
		const { container } = render(CardsPlayedChart, { props: { groups: [group] } });

		expect(captionText(container)).toMatch(/as often as/);
		expect(container.querySelector(".podium-caption .shake")).toBeNull();
	});

	it("caption says top when there is no baseline", () => {
		const group: CardsPlayedGroup = {
			id: "actions",
			rows: [{ key: "draw4", count: 31, ratio: 1 }]
		};
		const { container } = render(CardsPlayedChart, { props: { groups: [group] } });

		expect(container.querySelectorAll(".podium-card")).toHaveLength(1);
		expect(captionText(container)).toMatch(/\+4 is your most played action card/);
	});

	it("a single group input renders without a crash", () => {
		render(CardsPlayedChart, { props: { groups: [coloursGroup] } });

		expect(screen.getByRole("tablist")).toBeInTheDocument();
		expect(screen.getAllByRole("tab")).toHaveLength(1);
		expect(screen.getByRole("tab", { name: "Colors" })).toHaveAttribute("aria-selected", "true");
		expect(screen.getByText("Red")).toBeInTheDocument();
	});

	it("paints each color bar with the card color it describes", () => {
		const { container } = render(CardsPlayedChart, { props: { groups: allGroups() } });

		const styles = [...container.querySelectorAll(".bar")].map((bar) => bar.getAttribute("style"));
		expect(styles[0]).toContain("#bd3130");
		expect(styles[1]).toContain("#0470dd");
		expect(styles[2]).toContain("#4aab42");
		expect(styles[3]).toContain("#f2cc47");
	});

	it("keeps the accent color for value bars", async () => {
		const { container } = render(CardsPlayedChart, { props: { groups: allGroups() } });

		await fireEvent.click(screen.getByRole("tab", { name: "Values" }));

		for (const bar of container.querySelectorAll(".bar")) {
			expect(bar.getAttribute("style")).toContain("var(--accent)");
		}
	});

	it("draws a real card on the most played action card", () => {
		const { container } = render(CardsPlayedChart, { props: { groups: [actionsGroup] } });

		const cards = container.querySelectorAll(".podium-card");
		expect(cards).toHaveLength(1);
		expect(cards[0].querySelector('[data-testid="loader-card"]')).not.toBeNull();
	});

	it("colors the action card #2c354d", () => {
		const { container } = render(CardsPlayedChart, { props: { groups: [actionsGroup] } });

		const card = container.querySelector('.podium-card [data-testid="loader-card"]');
		expect(card?.getAttribute("style")).toContain("--card-color: #2c354d");
	});

	it("keeps the pixel border on the tabs only", () => {
		render(CardsPlayedChart, { props: { groups: allGroups() } });

		for (const tab of screen.getAllByRole("tab")) {
			expect(tab).toHaveClass("pixel-bordered");
		}
	});
});
