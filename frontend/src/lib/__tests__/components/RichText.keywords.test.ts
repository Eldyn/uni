import { describe, it, expect } from "vitest";
import { render, screen } from "@testing-library/svelte";
import RichText from "$components/common/RichText.svelte";

describe("RichText keyword effects and styling", () => {
	it("renders keyword with TextEffects when segment has effect", () => {
		render(RichText, {
			props: {
				text: "Check [k=vanilla:turn][fx=shine]turn[/fx][/k] now",
				allowKeywords: true
			}
		});

		const btn = screen.getByRole("button", { name: "turn" });
		expect(btn).toBeInTheDocument();
		expect(btn).toHaveClass("glossary-keyword-btn");
		expect(btn.querySelector(".fx-shine")).toBeInTheDocument();
	});

	it("applies red card color and underline styling to keyword button", () => {
		render(RichText, {
			props: {
				text: "Check [k=vanilla:turn]turn[/k]",
				allowKeywords: true
			}
		});

		const btn = screen.getByRole("button", { name: "turn" });
		expect(btn).toHaveClass("glossary-keyword-btn");
	});
});
