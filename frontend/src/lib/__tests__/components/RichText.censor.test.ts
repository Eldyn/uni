import { describe, it, expect } from "vitest";
import { render, screen } from "@testing-library/svelte";
import RichText from "$components/common/RichText.svelte";
import { loadCensorData } from "$utils/censor.svelte";

describe("RichText censoring behavior", () => {
	it("does not censor uni-made content by default (e.g. 'pass' remains 'pass')", () => {
		render(RichText, {
			props: {
				text: "Stack +2 or +4 cards to pass the accumulated draw penalty to the next player."
			}
		});

		expect(
			screen.getByText("Stack +2 or +4 cards to pass the accumulated draw penalty to the next player.")
		).toBeInTheDocument();
		expect(screen.queryByText(/p\*\*\*/)).not.toBeInTheDocument();
	});

	it("preserves 'pass' when keywords are allowed in tooltips and descriptions", () => {
		render(RichText, {
			props: {
				text: "You can [k=play]pass[/k] your [k=turn]turn[/k].",
				allowKeywords: true
			}
		});

		const keywordBtn = screen.getByRole("button", { name: "pass" });
		expect(keywordBtn).toBeInTheDocument();
		expect(screen.queryByText(/p\*\*\*/)).not.toBeInTheDocument();
	});

	it("censors profane words only when censor={true} (chat messages)", async () => {
		await loadCensorData();
		render(RichText, {
			props: {
				text: "fuck this game",
				censor: true
			}
		});

		expect(screen.getByText("**** this game")).toBeInTheDocument();
		expect(screen.queryByText("fuck this game")).not.toBeInTheDocument();
	});
});
