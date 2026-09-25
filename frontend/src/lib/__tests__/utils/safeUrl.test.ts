import { describe, it, expect } from "vitest";
import { safeAvatarUrl } from "$lib/utils/safeUrl";

describe("safeAvatarUrl", () => {
	it("accepts relative, same-origin and data:image URLs", () => {
		expect(safeAvatarUrl("/avatars/a.png", "https://playuni.app")).toBe("/avatars/a.png");
		expect(safeAvatarUrl("https://playuni.app/a.png", "https://playuni.app")).toBe(
			"https://playuni.app/a.png"
		);
		expect(safeAvatarUrl("data:image/png;base64,AAAA", "https://playuni.app")).toBe(
			"data:image/png;base64,AAAA"
		);
	});
	it("rejects cross-origin, javascript: and CSS-breaking values", () => {
		expect(safeAvatarUrl("https://evil.example/a.png", "https://playuni.app")).toBe("");
		expect(safeAvatarUrl("javascript:alert(1)", "https://playuni.app")).toBe("");
		expect(safeAvatarUrl("x'); background:url(//evil)", "https://playuni.app")).toBe("");
	});
});
