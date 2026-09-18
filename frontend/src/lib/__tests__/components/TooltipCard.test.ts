import { describe, it, expect, vi } from "vitest";
import { render, screen, fireEvent } from "@testing-library/svelte";
import TooltipCard from "$components/common/TooltipCard.svelte";

describe("TooltipCard component", () => {
	it("renders title, content, and tag pills", () => {
		render(TooltipCard, {
			props: {
				title: "Fibonacci",
				tags: [{ label: "Uncommon", bg: "#2ca972" }]
			}
		});

		expect(screen.getByText("Fibonacci")).toBeInTheDocument();
		expect(screen.getByText("Uncommon")).toBeInTheDocument();
		const tagEl = screen.getByText("Uncommon");
		expect(tagEl.closest(".balatro-tag-pill")).toBeInTheDocument();
	});

	it("triggers onclose when clicking tooltip card outside links/buttons", async () => {
		const onclose = vi.fn();
		render(TooltipCard, {
			props: {
				title: "Turn",
				onclose
			}
		});

		const card = screen.getByText("Turn").closest(".balatro-tooltip-card")!;
		await fireEvent.click(card);
		expect(onclose).toHaveBeenCalledTimes(1);
	});
});
