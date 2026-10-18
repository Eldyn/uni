import { afterAll, beforeAll, beforeEach, describe, expect, it, vi } from "vitest";
import { CanvasTexture, NearestFilter, SRGBColorSpace } from "three";
import { createImage, type PixelImage } from "$lib/avatar/pixels";
import { FRAME_COUNT, FRAME_SIZE, STRIP_WIDTH, type Loadout } from "$lib/avatar/types";

type BrowserModule = typeof import("$lib/avatar/browser");

vi.mock("$lib/avatar/browser", async (importOriginal) => {
	const actual = await importOriginal<BrowserModule>();
	return { ...actual, decodePng: vi.fn() };
});

vi.mock("$lib/avatar/bakeCache", () => ({ getBakeCache: vi.fn() }));

import { getBakeCache } from "$lib/avatar/bakeCache";
import { decodePng } from "$lib/avatar/browser";
import { defaultLoadout } from "$lib/avatar/defaultLoadout";
import {
	BOT_STRIP_PATH,
	createAvatarTexture,
	loadAvatarTexture
} from "$components/game/three/avatarTexture";

const stripe: PixelImage = createImage(STRIP_WIDTH, FRAME_SIZE);

const humanLoadout: Loadout = { shape: "uni:round", skin: "basic:red", slots: {} };

/** jsdom has no 2D context; a no-op one is enough to build a CanvasTexture. */
beforeAll(() => {
	const fakeGetContext = function (this: HTMLCanvasElement) {
		return { canvas: this, putImageData: () => {} } as unknown as CanvasRenderingContext2D;
	} as unknown as HTMLCanvasElement["getContext"];
	vi.spyOn(HTMLCanvasElement.prototype, "getContext").mockImplementation(fakeGetContext);

	if (typeof globalThis.ImageData === "undefined") {
		vi.stubGlobal(
			"ImageData",
			class {
				data: Uint8ClampedArray;
				width: number;
				height: number;
				constructor(data: Uint8ClampedArray, width: number, height: number) {
					this.data = data;
					this.width = width;
					this.height = height;
				}
			}
		);
	}
});

afterAll(() => {
	vi.restoreAllMocks();
	vi.unstubAllGlobals();
});

beforeEach(() => {
	vi.clearAllMocks();
});

describe("createAvatarTexture", () => {
	it("sets sRGB colour space, nearest filter and repeat 1/5", () => {
		const texture = createAvatarTexture(stripe);

		expect(texture).toBeInstanceOf(CanvasTexture);
		expect(texture.colorSpace).toBe(SRGBColorSpace);
		expect(texture.magFilter).toBe(NearestFilter);
		expect(texture.minFilter).toBe(NearestFilter);
		expect(texture.repeat.x).toBeCloseTo(1 / FRAME_COUNT);
		expect(texture.repeat.y).toBe(1);
		expect(texture.offset.x).toBe(0);
	});
});

describe("loadAvatarTexture", () => {
	it("loads the bot strip and never bakes for a bot", async () => {
		const get = vi.fn();
		vi.mocked(getBakeCache).mockReturnValue({ get } as never);
		vi.mocked(decodePng).mockResolvedValue(stripe);

		const texture = await loadAvatarTexture(humanLoadout, true);

		expect(decodePng).toHaveBeenCalledWith(BOT_STRIP_PATH);
		expect(get).not.toHaveBeenCalled();
		expect(texture).toBeInstanceOf(CanvasTexture);
	});

	it("bakes a human loadout through the cache", async () => {
		const get = vi.fn().mockResolvedValue({ hash: "h", image: stripe });
		vi.mocked(getBakeCache).mockReturnValue({ get } as never);

		const texture = await loadAvatarTexture(humanLoadout, false);

		expect(get).toHaveBeenCalledTimes(1);
		expect(get).toHaveBeenCalledWith(humanLoadout);
		expect(texture).toBeInstanceOf(CanvasTexture);
	});

	it("bakes the default look when the loadout is missing", async () => {
		const get = vi.fn().mockResolvedValue({ hash: "d", image: stripe });
		vi.mocked(getBakeCache).mockReturnValue({ get } as never);

		await loadAvatarTexture(null, false);

		expect(get).toHaveBeenCalledWith(defaultLoadout());
	});

	it("falls back to the default loadout texture when a bake fails", async () => {
		const get = vi
			.fn()
			.mockRejectedValueOnce(new Error("bake failed"))
			.mockResolvedValueOnce({ hash: "d", image: stripe });
		vi.mocked(getBakeCache).mockReturnValue({ get } as never);

		const texture = await loadAvatarTexture(humanLoadout, false);

		expect(get).toHaveBeenCalledTimes(2);
		expect(get.mock.calls[1][0]).toEqual(defaultLoadout());
		expect(texture).toBeInstanceOf(CanvasTexture);
	});
});
