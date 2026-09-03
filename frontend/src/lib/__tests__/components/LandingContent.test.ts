import { describe, it, expect } from "vitest";
import { render, screen } from "@testing-library/svelte";
import LandingContent from "$components/landing/LandingContent.svelte";

describe("LandingContent", () => {
	it("renders the What Is This section with the trademark disclosure", () => {
		render(LandingContent);
		expect(screen.getByText(/what is this/i)).toBeInTheDocument();
		expect(screen.getByText(/mattel/i)).toBeInTheDocument();
	});

	it("renders the same/different comparison naming UNO", () => {
		render(LandingContent);
		expect(screen.getByText(/if you've played uno/i)).toBeInTheDocument();
	});

	it("names every custom rule from the lobby settings", () => {
		render(LandingContent);
		for (const rule of ["Draw Stacking", "Seven-Zero", "Jump In", "Force Play", "Progressive", "No Bluffing"]) {
			expect(screen.getByText(new RegExp(rule, "i"))).toBeInTheDocument();
		}
	});

	it("renders a Questions section covering the four core questions", () => {
		render(LandingContent);
		expect(screen.getByText(/need an account/i)).toBeInTheDocument();
		expect(screen.getByText(/how many people can play/i)).toBeInTheDocument();
		expect(screen.getByText(/work on (my )?phone/i)).toBeInTheDocument();
		expect(screen.getByText(/mattel/i)).toBeInTheDocument();
	});

	it("every section is a real DOM element, not conditionally rendered", () => {
		const { container } = render(LandingContent);
		expect(container.querySelectorAll("section").length).toBeGreaterThanOrEqual(6);
	});
});
