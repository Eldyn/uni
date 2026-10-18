import { beforeEach, describe, expect, it } from "vitest";
import { getItem, setCatalogueForTest } from "$lib/avatar/catalogue";
import { frameOffset } from "$lib/avatar/sockets";
import { avatarFixture } from "./avatarFixture";

describe("frameOffset", () => {
	beforeEach(() => {
		setCatalogueForTest(avatarFixture());
	});

	it("strip items have zero offset", () => {
		const item = getItem("uni:eyes_dot")!;

		expect(frameOffset("uni:round", item, 0)).toEqual([0, 0]);
		expect(frameOffset("uni:round", item, 3)).toEqual([0, 0]);
	});

	it("still items use socket plus bob for each frame", () => {
		const item = getItem("uni:hat_cap")!;

		expect(frameOffset("uni:round", item, 0)).toEqual([8, 4]);
		expect(frameOffset("uni:round", item, 1)).toEqual([8, 3]);
		expect(frameOffset("uni:round", item, 2)).toEqual([8, 2]);
		expect(frameOffset("uni:round", item, 3)).toEqual([8, 3]);
		expect(frameOffset("uni:round", item, 4)).toEqual([8, 4]);
	});

	it("a still item without a socket gives zero", () => {
		const item = getItem("uni:hat_float")!;

		expect(frameOffset("uni:round", item, 2)).toEqual([0, 0]);
	});

	it("an unknown shape gives zero", () => {
		const item = getItem("uni:hat_cap")!;

		expect(frameOffset("uni:missing", item, 2)).toEqual([0, 0]);
	});
});
