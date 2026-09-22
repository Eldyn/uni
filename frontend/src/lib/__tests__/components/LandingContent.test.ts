import { describe, it, expect } from "vitest";
import { render, screen, within } from "@testing-library/svelte";
import LandingContent from "$components/landing/LandingContent.svelte";

describe("LandingContent", () => {
	it("renders the What Is This section with the trademark disclosure", () => {
		const { container } = render(LandingContent);
		expect(screen.getByText(/what is this/i)).toBeInTheDocument();
		const disclosure = container.querySelector(".landing-legal");
		expect(disclosure).not.toBeNull();
		expect(within(disclosure as HTMLElement).getByText(/mattel/i)).toBeInTheDocument();
	});

	it("renders the same/different comparison naming UNO", () => {
		render(LandingContent);
		expect(screen.getByText(/if you've played uno/i)).toBeInTheDocument();
	});

	it("names every custom rule from the lobby settings", () => {
		render(LandingContent);
		for (const rule of [
			"Draw Stacking",
			"Seven-Zero",
			"Jump In",
			"Force Play",
			"Progressive",
			"No Bluffing"
		]) {
			expect(screen.getByText(new RegExp(rule, "i"))).toBeInTheDocument();
		}
	});

	it("renders a Questions section covering the four core questions", () => {
		const { container } = render(LandingContent);
		expect(screen.getByText(/need an account/i)).toBeInTheDocument();
		expect(screen.getByText(/how many people can play/i)).toBeInTheDocument();
		expect(screen.getByText(/work on (my )?phone/i)).toBeInTheDocument();
		const faq = container.querySelector(".landing-faq");
		expect(faq).not.toBeNull();
		expect(within(faq as HTMLElement).getByText(/endorsed by mattel/i)).toBeInTheDocument();
	});

	it("every section is a real DOM element, not conditionally rendered", () => {
		const { container } = render(LandingContent);
		expect(container.querySelectorAll("section").length).toBeGreaterThanOrEqual(6);
	});
});
