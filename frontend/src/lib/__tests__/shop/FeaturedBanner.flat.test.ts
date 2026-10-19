// The featured banner is a flat panel (owner ruling): no skew, no rotation,
// no perspective and no 3D style on the banner or on the avatar art in it.

import { describe, it, expect } from "vitest";
import bannerSource from "../../components/shop/FeaturedBanner.svelte?raw";
import spriteSource from "../../components/common/AvatarSprite.svelte?raw";

const TILT_PATTERNS: RegExp[] = [
	/perspective/i,
	/rotate[XYZ3]?\s*\(/i,
	/skew[XY]?\s*\(/i,
	/transform-style/i,
	/preserve-3d/i,
	/(^|[^-\w])transform\s*:/im
];

describe("FeaturedBanner flat panel", () => {
	it.each(TILT_PATTERNS.map((pattern) => [String(pattern), pattern]))(
		"the banner has no %s",
		(_name, pattern) => {
			expect(bannerSource).not.toMatch(pattern as RegExp);
		}
	);

	it.each(TILT_PATTERNS.map((pattern) => [String(pattern), pattern]))(
		"the banner art has no %s",
		(_name, pattern) => {
			expect(spriteSource).not.toMatch(pattern as RegExp);
		}
	);
});
