import { describe, it, expect } from "vitest";
import {
	LOADER_CARD_COUNT,
	randomLoaderFace,
	randomLoaderFaces
} from "$lib/components/common/loader/loaderFaces";

const COLOURED = ["red", "blue", "green", "yellow"];
const COLOURED_VALUES = ["0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "skip", "reverse", "+2"];
const WILD_VALUES = ["jolly", "jolly_draw4"];

function sequence(values: number[]): () => number {
	let index = 0;
	return () => values[index++ % values.length];
}

describe("loaderFaces", () => {
	it("only yields valid colour/value pairs", () => {
		for (let i = 0; i < 2000; i++) {
			const face = randomLoaderFace();
			if (face.type === "white") {
				expect(WILD_VALUES).toContain(face.value);
			} else {
				expect(COLOURED).toContain(face.type);
				expect(COLOURED_VALUES).toContain(face.value);
			}
		}
	});

	it("can produce a wild card", () => {
		const face = randomLoaderFace(sequence([0.999999, 0.999999]));
		expect(face.type).toBe("white");
		expect(WILD_VALUES).toContain(face.value);
	});

	it("returns the requested number of faces", () => {
		expect(randomLoaderFaces()).toHaveLength(LOADER_CARD_COUNT);
		expect(randomLoaderFaces(3)).toHaveLength(3);
	});
});
