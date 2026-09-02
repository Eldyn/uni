import { describe, it, expect } from "vitest";
import { existsSync } from "node:fs";
import { resolve } from "node:path";

describe("Paraglide setup", () => {
	it("generated the runtime and messages modules", () => {
		expect(existsSync(resolve(__dirname, "../../paraglide/runtime.js"))).toBe(true);
		expect(existsSync(resolve(__dirname, "../../paraglide/messages.js"))).toBe(true);
	});

	it("knows about both locales", async () => {
		const runtime = await import("$lib/paraglide/runtime.js");
		expect(runtime.locales).toContain("en");
		expect(runtime.locales).toContain("it");
	});
});
