import { describe, it, expect } from "vitest";
import { resolveNavShape } from "$components/shell/navShape";

describe("resolveNavShape", () => {
	it("uses the bottom bar for a narrow, tall viewport (portrait phone)", () => {
		expect(resolveNavShape(390, 844)).toBe("bottom");
	});

	it("uses the rail for a wide viewport regardless of height (desktop)", () => {
		expect(resolveNavShape(1440, 900)).toBe("rail");
	});

	it("uses the rail for a narrow but short viewport (landscape phone) — the case width-only logic gets wrong", () => {
		expect(resolveNavShape(844, 390)).toBe("rail");
	});

	it("uses the rail at exactly the width threshold", () => {
		expect(resolveNavShape(768, 1024)).toBe("rail");
	});

	it("uses the rail at exactly the height threshold", () => {
		expect(resolveNavShape(500, 599)).toBe("rail");
	});

	it("uses the bottom bar just above the height threshold and below the width threshold", () => {
		expect(resolveNavShape(500, 600)).toBe("bottom");
	});
});
