// The bake cache fetches art from the served avatar asset root, while the
// catalogue stores paths relative to it. A raw catalogue path must be resolved
// under /assets/avatar so a baked avatar is never a hidden 404.

import { describe, expect, it } from "vitest";
import { avatarAssetUrl } from "$lib/avatar/bakeCache";

describe("avatarAssetUrl", () => {
	it("resolves a catalogue-relative path under the avatar asset root", () => {
		expect(avatarAssetUrl("shapes/uni_round.png")).toBe("/assets/avatar/shapes/uni_round.png");
	});

	it("resolves nested slot paths", () => {
		expect(avatarAssetUrl("head_top/uni_hat_cap.png")).toBe(
			"/assets/avatar/head_top/uni_hat_cap.png"
		);
	});
});
