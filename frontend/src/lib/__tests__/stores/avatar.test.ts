import { describe, it, expect, vi, beforeEach } from "vitest";
import { storeAvatar } from "$stores/avatar.svelte";
import type { Loadout } from "$lib/avatar/types";

global.fetch = vi.fn();

const sample: Loadout = {
	shape: "uni:shape_round",
	skin: "uni:skin_basic_1",
	slots: {
		eyes: { item: "uni:eyes_dot", colours: [] },
		mouth: { item: "uni:mouth_line", colours: [] }
	}
};

describe("storeAvatar", () => {
	beforeEach(() => {
		vi.resetAllMocks();
		storeAvatar.reset();
	});

	it("storeAvatar.fetchMine stores the loadout", async () => {
		vi.mocked(fetch).mockResolvedValueOnce(
			new Response(JSON.stringify({ loadout: sample }), { status: 200 })
		);

		await storeAvatar.fetchMine();

		expect(fetch).toHaveBeenCalledWith("/avatar/loadout/me", {
			credentials: "include",
			headers: { "Content-Type": "application/json" }
		});
		expect(storeAvatar.loadout).toEqual(sample);
	});

	it("storeAvatar.save sends PUT and stores the result", async () => {
		vi.mocked(fetch).mockResolvedValueOnce(
			new Response(JSON.stringify({ loadout: sample }), { status: 200 })
		);

		const result = await storeAvatar.save(sample);

		expect(result).toEqual({ ok: true });
		expect(fetch).toHaveBeenCalledWith("/avatar/loadout/me", {
			method: "PUT",
			credentials: "include",
			headers: { "Content-Type": "application/json" },
			body: JSON.stringify(sample)
		});
		expect(storeAvatar.loadout).toEqual(sample);
	});

	it("storeAvatar.save returns the error code on a 400", async () => {
		vi.mocked(fetch).mockResolvedValueOnce(
			new Response(JSON.stringify({ error: "not_owned", detail: "uni:hat_cone" }), { status: 400 })
		);

		const result = await storeAvatar.save(sample);

		expect(result).toEqual({ ok: false, code: "not_owned" });
	});

	it("storeAvatar.reset clears the loadout", async () => {
		vi.mocked(fetch).mockResolvedValueOnce(
			new Response(JSON.stringify({ loadout: sample }), { status: 200 })
		);
		await storeAvatar.fetchMine();
		expect(storeAvatar.loadout).not.toBeNull();

		storeAvatar.reset();

		expect(storeAvatar.loadout).toBeNull();
	});
});
