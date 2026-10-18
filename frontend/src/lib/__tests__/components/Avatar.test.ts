import { describe, it, expect, vi, beforeEach } from "vitest";
import { render } from "@testing-library/svelte";
import Avatar from "$components/common/Avatar.svelte";

const baked = vi.hoisted(() => ({
	set: vi.fn(),
	destroy: vi.fn(),
	state: { url: null as string | null }
}));

vi.mock("$lib/avatar/useBakedAvatar.svelte", () => ({
	createBakedAvatar: () => ({
		get url() {
			return baked.state.url;
		},
		set: baked.set,
		destroy: baked.destroy
	})
}));

const STRIP_URL = "blob:http://localhost/baked-strip";
const BOT_STRIP = "/assets/bot_animated_strip.png";

beforeEach(() => {
	vi.clearAllMocks();
	baked.state.url = STRIP_URL;
});

describe("Avatar", () => {
	it("renders a sprite of the requested size", () => {
		const { container } = render(Avatar, { props: { size: 48 } });
		const sprite = container.querySelector(".avatar-sprite") as HTMLElement;

		expect(sprite).toBeTruthy();
		expect(sprite.style.width).toBe("48px");
		expect(sprite.style.height).toBe("48px");
	});

	it("bot renders the bot strip and never bakes", () => {
		const { container } = render(Avatar, { props: { bot: true, size: 32 } });
		const sprite = container.querySelector(".avatar-sprite") as HTMLElement;

		expect(sprite.style.getPropertyValue("background-image")).toContain(BOT_STRIP);
		expect(baked.set).not.toHaveBeenCalled();
	});

	it("a null loadout renders the default avatar", () => {
		const { container } = render(Avatar, { props: { loadout: null, size: 32 } });
		const sprite = container.querySelector(".avatar-sprite") as HTMLElement;

		expect(sprite).toBeTruthy();
		expect(sprite.style.getPropertyValue("background-image")).toContain(STRIP_URL);
		expect(baked.set).toHaveBeenCalledWith(null);
	});

	it("ignores the legacy src prop", () => {
		const { container } = render(Avatar, {
			props: { src: "/assets/base_player.gif", size: 32 }
		});

		expect(container.innerHTML).not.toContain("base_player.gif");
		const sprite = container.querySelector(".avatar-sprite") as HTMLElement;
		expect(sprite.style.getPropertyValue("background-image")).toContain(STRIP_URL);
		expect(baked.set).toHaveBeenCalled();
	});

	it("keeps its box size while the bake is pending", () => {
		baked.state.url = null;

		const { container } = render(Avatar, { props: { size: 32 } });
		const box = container.querySelector(".avatar-placeholder") as HTMLElement;

		expect(box).toBeTruthy();
		expect(box.style.width).toBe("32px");
		expect(box.style.height).toBe("32px");
		expect(container.querySelector(".avatar-sprite")).toBeNull();
	});

	it("unmount revokes the url", () => {
		const { unmount } = render(Avatar, { props: { size: 32 } });

		unmount();

		expect(baked.destroy).toHaveBeenCalledTimes(1);
	});
});
