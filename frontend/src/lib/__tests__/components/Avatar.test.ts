import { describe, it, expect } from "vitest";
import { render } from "@testing-library/svelte";
import Avatar from "$components/common/Avatar.svelte";

describe("Avatar", () => {
	it("defaults to the base player sprite at size 32", () => {
		const { container } = render(Avatar);
		const sprite = container.querySelector(".tinted-sprite") as HTMLElement;
		expect(sprite).toBeTruthy();
		expect(sprite.style.getPropertyValue("--sprite-img")).toContain("/assets/base_player.gif");
		expect(sprite.style.width).toBe("32px");
		expect(sprite.style.height).toBe("32px");
	});

	it("renders a custom src at a discrete size", () => {
		const { container } = render(Avatar, { props: { src: "/assets/skins/red.gif", size: 64 } });
		const sprite = container.querySelector(".tinted-sprite") as HTMLElement;
		expect(sprite.style.getPropertyValue("--sprite-img")).toContain("/assets/skins/red.gif");
		expect(sprite.style.width).toBe("64px");
	});
});
