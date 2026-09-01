import { describe, it, expect } from "vitest";
import { render } from "@testing-library/svelte";
import TintedSprite from "$components/common/TintedSprite.svelte";

describe("TintedSprite", () => {
	it("defaults to a 32px integer size step", () => {
		const { container } = render(TintedSprite, {
			props: { src: "/avatar.png", color: "#c084fc" }
		});
		const el = container.querySelector(".tinted-sprite") as HTMLElement;
		expect(el.style.width).toBe("32px");
		expect(el.style.height).toBe("32px");
	});

	it("accepts an explicit size step", () => {
		const { container } = render(TintedSprite, {
			props: { src: "/avatar.png", color: "#c084fc", size: 64 }
		});
		const el = container.querySelector(".tinted-sprite") as HTMLElement;
		expect(el.style.width).toBe("64px");
		expect(el.style.height).toBe("64px");
	});

	it("sets pixelated image-rendering explicitly", () => {
		const { container } = render(TintedSprite, {
			props: { src: "/avatar.png", color: "#c084fc" }
		});
		const el = container.querySelector(".tinted-sprite") as HTMLElement;
		expect(getComputedStyle(el).imageRendering).toBe("pixelated");
	});
});
