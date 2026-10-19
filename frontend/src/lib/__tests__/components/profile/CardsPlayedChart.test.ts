import { describe, it, expect } from "vitest";
import { render, screen, fireEvent } from "@testing-library/svelte";
import { tick } from "svelte";

import CardsPlayedChart from "$components/profile/CardsPlayedChart.svelte";
import type { CardsPlayedGroup } from "$components/profile/profileModel";

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

function podiumFaces(): (string | null)[] {
	return screen
		.getAllByRole("button")
		.map((button) => button.querySelector(".podium-face")?.textContent?.trim() ?? null);
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

	it("actions podium puts the most used card in the middle", () => {
		render(CardsPlayedChart, { props: { groups: [actionsGroup] } });

		expect(podiumFaces()).toEqual(["Wild", "Skip", "+4", "+2"]);
		expect(screen.getByRole("button", { name: /\+4/ })).toHaveAttribute("aria-pressed", "true");
	});

	it("clicking a side card recentres the podium and updates the caption", async () => {
		render(CardsPlayedChart, { props: { groups: [actionsGroup] } });

		await fireEvent.click(screen.getByRole("button", { name: /Skip/ }));

		expect(podiumFaces()).toEqual(["Wild", "+4", "Skip", "+2"]);
		expect(screen.getByRole("button", { name: /Skip/ })).toHaveAttribute("aria-pressed", "true");
		expect(screen.getByText(/15% more/)).toBeInTheDocument();
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
		render(CardsPlayedChart, { props: { groups: [group] } });

		const caption = screen.getByText(/40% more/);
		expect(caption).toBeInTheDocument();
		expect(caption).toHaveAttribute("aria-live", "polite");
	});

	it("caption says less with a negative percent and shows the absolute value", async () => {
		const group: CardsPlayedGroup = {
			id: "actions",
			rows: [
				{ key: "draw4", count: 20, ratio: 1 },
				{ key: "skip", count: 10, ratio: 0.5 },
				{ key: "draw2", count: 4, ratio: 0.2 }
			]
		};
		render(CardsPlayedChart, { props: { groups: [group] } });

		await fireEvent.click(screen.getByRole("button", { name: /\+2/ }));

		expect(screen.getByText(/73% less/)).toBeInTheDocument();
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
		render(CardsPlayedChart, { props: { groups: [group] } });

		expect(screen.getByText(/as often as/)).toBeInTheDocument();
	});

	it("caption says top when there is no baseline", () => {
		const group: CardsPlayedGroup = {
			id: "actions",
			rows: [{ key: "draw4", count: 31, ratio: 1 }]
		};
		render(CardsPlayedChart, { props: { groups: [group] } });

		expect(podiumFaces()).toEqual(["+4"]);
		expect(screen.getByRole("button", { name: /\+4/ })).toHaveAttribute("aria-pressed", "true");
		expect(screen.getByText(/\+4 is your most played action card/)).toBeInTheDocument();
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
});
