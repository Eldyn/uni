import { describe, it, expect, vi, beforeEach } from "vitest";
import { render } from "@testing-library/svelte";
import OccupancyGauge from "$components/lobby/OccupancyGauge.svelte";
import { seatLoadout } from "$lib/avatar/randomLoadout";

const baked = vi.hoisted(() => ({ set: vi.fn(), destroy: vi.fn() }));

vi.mock("$lib/avatar/useBakedAvatar.svelte", () => ({
	createBakedAvatar: () => ({
		get url() {
			return "blob:seat";
		},
		set: baked.set,
		destroy: baked.destroy
	})
}));

beforeEach(() => baked.set.mockClear());

const AVATAR_SELECTOR = ".avatar-sprite, .avatar-placeholder";
const AVATAR_GIF = /base_player\.gif|bot_animated\.gif/;

describe("OccupancyGauge avatars", () => {
	it("renders avatars through Avatar", () => {
		const { container } = render(OccupancyGauge, { props: { filled: 2, max: 4 } });

		expect(container.querySelectorAll(AVATAR_SELECTOR)).toHaveLength(2);
	});

	it("references no avatar gif", () => {
		const { container } = render(OccupancyGauge, { props: { filled: 2, max: 4 } });

		expect(container.innerHTML).not.toMatch(AVATAR_GIF);
	});
});

describe("OccupancyGauge seat looks", () => {
	it("bakes a seeded starter look for each filled seat icon", () => {
		render(OccupancyGauge, { props: { filled: 4, max: 4, seedKey: "ABC" } });

		const baked_loadouts = baked.set.mock.calls.map((call) => call[0]);
		expect(baked_loadouts).toEqual([0, 1, 2, 3].map((seat) => seatLoadout("ABC", seat)));
	});

	it("never bakes a null loadout", () => {
		render(OccupancyGauge, { props: { filled: 4, max: 4, seedKey: "ABC" } });

		for (const call of baked.set.mock.calls) expect(call[0]).not.toBeNull();
	});

	it("keeps the same looks when the lobby re-renders", () => {
		const first = render(OccupancyGauge, { props: { filled: 2, max: 4, seedKey: "XYZ" } });
		const firstCalls = baked.set.mock.calls.map((call) => call[0]);
		first.unmount();
		baked.set.mockClear();

		render(OccupancyGauge, { props: { filled: 2, max: 4, seedKey: "XYZ" } });

		expect(baked.set.mock.calls.map((call) => call[0])).toEqual(firstCalls);
	});
});
