import { beforeEach, describe, expect, it } from "vitest";
import { getCatalogue, setCatalogueForTest } from "$lib/avatar/catalogue";
import { defaultLoadout } from "$lib/avatar/defaultLoadout";
import { avatarFixture } from "./avatarFixture";

describe("defaultLoadout", () => {
	beforeEach(() => {
		setCatalogueForTest(avatarFixture());
	});

	it("validates against the catalogue fixture", () => {
		const loadout = defaultLoadout();
		const catalogue = getCatalogue();

		const shape = catalogue.items.find((item) => item.id === loadout.shape);
		const eyes = catalogue.items.find((item) => item.id === loadout.slots.eyes?.item);
		const mouth = catalogue.items.find((item) => item.id === loadout.slots.mouth?.item);

		expect(shape).toMatchObject({ slot: "shape", starter: true });
		expect(eyes).toMatchObject({ slot: "eyes", starter: true });
		expect(mouth).toMatchObject({ slot: "mouth", starter: true });
		expect(loadout.skin).toBe("basic:red");
		expect(Object.keys(loadout.slots).sort()).toEqual(["eyes", "mouth"]);
	});
});
