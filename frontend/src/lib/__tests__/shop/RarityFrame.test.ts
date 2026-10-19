import { afterEach, describe, expect, it } from "vitest";
import { cleanup, render } from "@testing-library/svelte";
import { createRawSnippet } from "svelte";
import RarityFrame from "$components/shop/RarityFrame.svelte";
import type { Rarity } from "$lib/avatar/types";

const children = createRawSnippet(() => ({ render: () => "<span>art</span>" }));

function frame(rarity: Rarity) {
	const { container } = render(RarityFrame, { props: { rarity, children } });
	return {
		shimmer: container.querySelector(".rarity-shimmer") !== null,
		sunburst: container.querySelector(".rarity-sunburst") !== null
	};
}

afterEach(cleanup);

describe("RarityFrame", () => {
	it("animates rare with a shimmer, epic and special with a shimmer and a sunburst", () => {
		expect(frame("basic")).toEqual({ shimmer: false, sunburst: false });
		expect(frame("common")).toEqual({ shimmer: false, sunburst: false });
		expect(frame("rare")).toEqual({ shimmer: true, sunburst: false });
		expect(frame("epic")).toEqual({ shimmer: true, sunburst: true });
		expect(frame("special")).toEqual({ shimmer: true, sunburst: true });
	});
});
