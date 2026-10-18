import { describe, it, expect } from "vitest";
import { render, screen } from "@testing-library/svelte";
import AvatarSprite, { bobDelayMs } from "$components/common/AvatarSprite.svelte";
import { FRAME_COUNT, FRAME_MS } from "$lib/avatar/types";

const LOOP_MS = FRAME_COUNT * FRAME_MS;
const STRIP_URL = "blob:http://localhost/avatar-strip";

describe("AvatarSprite", () => {
	it("renders the strip as the background at 5x width", () => {
		const { container } = render(AvatarSprite, { props: { url: STRIP_URL, size: 96 } });
		const sprite = container.querySelector(".avatar-sprite") as HTMLElement;

		expect(sprite.style.getPropertyValue("background-image")).toContain(STRIP_URL);
		expect(sprite.style.getPropertyValue("background-size")).toBe("480px 96px");
		expect(sprite.style.width).toBe("96px");
		expect(sprite.style.height).toBe("96px");
	});

	it("size 32 gives a 160px wide background", () => {
		const { container } = render(AvatarSprite, { props: { url: STRIP_URL, size: 32 } });
		const sprite = container.querySelector(".avatar-sprite") as HTMLElement;

		expect(sprite.style.getPropertyValue("background-size")).toBe("160px 32px");
	});

	it("the animation uses steps(5) and a 600ms duration", () => {
		const { container } = render(AvatarSprite, { props: { url: STRIP_URL, size: 32 } });
		const sprite = container.querySelector(".avatar-sprite") as HTMLElement;
		const animation = sprite.style.getPropertyValue("animation");

		expect(animation).toContain("steps(5)");
		expect(animation).toContain("600ms");
	});

	it("bobDelayMs is the same for the same clock reading and negative", () => {
		expect(bobDelayMs(1234)).toBe(bobDelayMs(1234));
		expect(bobDelayMs(1234)).toBeLessThan(0);
	});

	it("bobDelayMs wraps at the loop length", () => {
		expect(bobDelayMs(100)).toBe(bobDelayMs(100 + LOOP_MS));
		expect(bobDelayMs(100)).toBe(-100);
	});

	it("has an accessible name only when a label is given", () => {
		const { unmount } = render(AvatarSprite, {
			props: { url: STRIP_URL, size: 32, label: "Ada" }
		});
		expect(screen.getByRole("img", { name: "Ada" })).toBeTruthy();
		unmount();

		const { container } = render(AvatarSprite, { props: { url: STRIP_URL, size: 32 } });
		const sprite = container.querySelector(".avatar-sprite") as HTMLElement;

		expect(sprite.getAttribute("aria-hidden")).toBe("true");
		expect(screen.queryByRole("img")).toBeNull();
	});
});
