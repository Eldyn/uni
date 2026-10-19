import { describe, expect, it } from "vitest";
import { MAX_REGIONS } from "$lib/avatar/types";

const catalogues = import.meta.glob("../../../../messages/*.json", {
	eager: true,
	import: "default"
}) as Record<string, Record<string, string>>;

describe("wardrobe region labels", () => {
	for (const [file, catalogue] of Object.entries(catalogues)) {
		it(`${file} has a label for every region up to MAX_REGIONS`, () => {
			for (let region = 1; region <= MAX_REGIONS; region++) {
				expect(catalogue, `missing wardrobe_region_${region}`).toHaveProperty(
					`wardrobe_region_${region}`
				);
			}
		});
	}
});
