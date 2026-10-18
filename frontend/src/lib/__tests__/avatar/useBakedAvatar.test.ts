import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { setCatalogueForTest } from "$lib/avatar/catalogue";
import { defaultLoadout } from "$lib/avatar/defaultLoadout";
import { createImage, type PixelImage } from "$lib/avatar/pixels";
import { FRAME_SIZE, STRIP_WIDTH, type Loadout } from "$lib/avatar/types";
import { createBakedAvatar, type BakeCacheLike } from "$lib/avatar/useBakedAvatar.svelte";
import { avatarFixture, fullLoadout } from "./avatarFixture";

type Strip = { image: PixelImage };

interface FakeCache extends BakeCacheLike {
	calls: Loadout[];
}

function fakeCache(failOnCall?: number): FakeCache {
	const calls: Loadout[] = [];
	return {
		calls,
		async get(loadout: Loadout): Promise<Strip> {
			calls.push(loadout);
			if (calls.length === failOnCall) throw new Error("bake failed");
			return { image: createImage(STRIP_WIDTH, FRAME_SIZE) };
		}
	};
}

function blobUrls(): { toBlobUrl: (img: PixelImage) => Promise<string>; urls: string[] } {
	const urls: string[] = [];
	return {
		urls,
		async toBlobUrl() {
			const url = `blob:${urls.length + 1}`;
			urls.push(url);
			return url;
		}
	};
}

async function settle(): Promise<void> {
	await new Promise((resolve) => setTimeout(resolve, 0));
}

const originalRevoke = URL.revokeObjectURL;
let revoked: string[];

beforeEach(() => {
	setCatalogueForTest(avatarFixture());
	revoked = [];
	(URL as unknown as { revokeObjectURL: (url: string) => void }).revokeObjectURL = (url) => {
		revoked.push(url);
	};
});

afterEach(() => {
	if (originalRevoke) {
		URL.revokeObjectURL = originalRevoke;
	} else {
		delete (URL as unknown as { revokeObjectURL?: (url: string) => void }).revokeObjectURL;
	}
});

describe("createBakedAvatar", () => {
	it("url is null before the first bake", () => {
		const cache = fakeCache();
		const avatar = createBakedAvatar({ cache, toBlobUrl: blobUrls().toBlobUrl });

		expect(avatar.url).toBeNull();

		avatar.set(fullLoadout());
		expect(avatar.url).toBeNull();
	});

	it("url is set after the bake resolves", async () => {
		const cache = fakeCache();
		const avatar = createBakedAvatar({ cache, toBlobUrl: blobUrls().toBlobUrl });

		avatar.set(fullLoadout());
		await settle();

		expect(avatar.url).toBe("blob:1");
		expect(revoked).toEqual([]);
	});

	it("set with null bakes the default loadout", async () => {
		const cache = fakeCache();
		const avatar = createBakedAvatar({ cache, toBlobUrl: blobUrls().toBlobUrl });

		avatar.set(null);
		await settle();

		expect(cache.calls).toHaveLength(1);
		expect(cache.calls[0]).toEqual(defaultLoadout());
	});

	it("setting an equal loadout twice bakes once", async () => {
		const cache = fakeCache();
		const avatar = createBakedAvatar({ cache, toBlobUrl: blobUrls().toBlobUrl });

		avatar.set(fullLoadout());
		avatar.set(fullLoadout());
		await settle();

		expect(cache.calls).toHaveLength(1);
	});

	it("a bake error keeps the previous url and does not throw", async () => {
		const cache = fakeCache(2);
		const avatar = createBakedAvatar({ cache, toBlobUrl: blobUrls().toBlobUrl });

		avatar.set(fullLoadout());
		await settle();

		const other = fullLoadout({
			slots: { ...fullLoadout().slots, eyes: { item: "uni:eyes_wide", colours: [] } }
		});
		expect(() => avatar.set(other)).not.toThrow();
		await settle();

		expect(avatar.url).toBe("blob:1");
	});

	it("the previous url is revoked when a new one replaces it", async () => {
		const cache = fakeCache();
		const avatar = createBakedAvatar({ cache, toBlobUrl: blobUrls().toBlobUrl });

		avatar.set(fullLoadout());
		await settle();

		const other = fullLoadout({
			slots: { ...fullLoadout().slots, eyes: { item: "uni:eyes_wide", colours: [] } }
		});
		avatar.set(other);
		await settle();

		expect(avatar.url).toBe("blob:2");
		expect(revoked).toEqual(["blob:1"]);
	});

	it("destroy revokes the current url", async () => {
		const cache = fakeCache();
		const avatar = createBakedAvatar({ cache, toBlobUrl: blobUrls().toBlobUrl });

		avatar.set(fullLoadout());
		await settle();

		avatar.destroy();

		expect(revoked).toEqual(["blob:1"]);
		expect(avatar.url).toBeNull();
	});
});
