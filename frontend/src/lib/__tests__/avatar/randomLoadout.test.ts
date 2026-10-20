import { afterEach, beforeEach, describe, expect, it } from "vitest";
import { getCatalogue, setCatalogueForTest } from "$lib/avatar/catalogue";
import { randomStarterLoadout, seatLoadout, starterSwatchIds } from "$lib/avatar/randomLoadout";
import { avatarFixture } from "./avatarFixture";
import type { Catalogue } from "$lib/avatar/types";

function fixtureWithManyStarters(): Catalogue {
	const base = avatarFixture();
	base.palettes[0].swatches.push(
		{ id: "basic:blue", hex: "#4a6ae8" },
		{ id: "basic:gold", hex: "#e8c84a" },
		{ id: "basic:none", hex: "#000000", pattern: "none" }
	);
	base.items.push({
		id: "uni:tall",
		slot: "shape",
		name: "Tall",
		asset: { kind: "strip", path: "shapes/uni_tall.png" },
		regions: 1,
		rarity: "common",
		price: 0,
		starter: true
	});
	return base;
}

beforeEach(() => setCatalogueForTest(fixtureWithManyStarters()));
afterEach(() => setCatalogueForTest(avatarFixture()));

describe("randomStarterLoadout", () => {
	it("gives the same loadout for the same seed", () => {
		expect(randomStarterLoadout("ABC:0")).toEqual(randomStarterLoadout("ABC:0"));
	});

	it("gives different loadouts for different seeds", () => {
		const seen = new Set<string>();
		for (let seat = 0; seat < 30; seat++) {
			seen.add(JSON.stringify(randomStarterLoadout(`ABC:${seat}`)));
		}
		expect(seen.size).toBeGreaterThan(5);
	});

	it("uses only starter items and starter swatches, with one color per region", () => {
		const catalogue = getCatalogue();
		const starterItems = new Set(
			catalogue.items.filter((item) => item.starter === true).map((item) => item.id)
		);
		const starterSwatches = new Set(
			catalogue.palettes
				.filter((palette) => palette.starter === true)
				.flatMap((palette) => palette.swatches.map((swatch) => swatch.id))
		);

		for (let seat = 0; seat < 30; seat++) {
			const loadout = randomStarterLoadout(`LOBBY:${seat}`);
			expect(starterItems.has(loadout.shape)).toBe(true);
			expect(starterSwatches.has(loadout.skin)).toBe(true);
			for (const choice of Object.values(loadout.slots)) {
				expect(starterItems.has(choice.item)).toBe(true);
				const item = catalogue.items.find((entry) => entry.id === choice.item);
				expect(choice.colours).toHaveLength(item?.regions ?? -1);
				for (const swatchId of choice.colours) expect(starterSwatches.has(swatchId)).toBe(true);
			}
		}
	});

	it("never picks the reset sentinel", () => {
		expect(starterSwatchIds()).not.toContain("basic:none");

		for (let seat = 0; seat < 30; seat++) {
			const loadout = randomStarterLoadout(`RESET:${seat}`);
			expect(loadout.skin).not.toBe("basic:none");
			for (const choice of Object.values(loadout.slots)) {
				expect(choice.colours).not.toContain("basic:none");
			}
		}
	});
});

describe("seatLoadout", () => {
	it("seeds with the lobby id and the seat index", () => {
		expect(seatLoadout("ABC", 2)).toEqual(randomStarterLoadout("ABC:2"));
	});

	it("gives different seats of one lobby different seeds", () => {
		const seats = [0, 1, 2, 3].map((seat) => JSON.stringify(seatLoadout("ABC", seat)));
		expect(new Set(seats).size).toBeGreaterThan(1);
	});
});
