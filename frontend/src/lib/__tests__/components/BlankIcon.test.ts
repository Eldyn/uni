import { describe, it, expect } from "vitest";
import { render, screen } from "@testing-library/svelte";
import BlankIcon from "$components/common/BlankIcon.svelte";

describe("BlankIcon", () => {
	it("is a hidden decoration 0.6em by 0.85em by default", () => {
		const { container } = render(BlankIcon);
		const icon = container.querySelector(".blank-icon");
		expect(icon).not.toBeNull();
		expect(icon).toHaveAttribute("aria-hidden", "true");
		expect(icon).not.toHaveAttribute("role");
		expect(icon?.getAttribute("style")).toContain("width: 0.6em");
		expect(icon?.getAttribute("style")).toContain("height: 0.85em");
	});

	it("scales the width with the size prop", () => {
		const { container } = render(BlankIcon, { props: { size: 1.7 } });
		const style = container.querySelector(".blank-icon")?.getAttribute("style");
		expect(style).toContain("width: 1.2em");
		expect(style).toContain("height: 1.7em");
	});

	it("is an image named by its label, with the same tooltip", () => {
		render(BlankIcon, { props: { label: "Blank" } });
		const icon = screen.getByRole("img", { name: "Blank" });
		expect(icon).toHaveAttribute("title", "Blank");
		expect(icon).not.toHaveAttribute("aria-hidden");
	});

	it("has no text content", () => {
		const { container } = render(BlankIcon, { props: { label: "Blank" } });
		expect(container.querySelector(".blank-icon")?.textContent).toBe("");
	});
});
