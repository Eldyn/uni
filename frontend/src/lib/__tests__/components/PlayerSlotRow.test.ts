import { describe, it, expect, vi } from "vitest";
import { render } from "@testing-library/svelte";

// The real Avatar bakes through this helper; the fake records any bake so the
// test can prove PlayerSlotRow never bakes the red default avatar.
const baked = vi.hoisted(() => ({ set: vi.fn(), destroy: vi.fn() }));
vi.mock("$lib/avatar/useBakedAvatar.svelte", () => ({
	createBakedAvatar: () => ({ url: null, set: baked.set, destroy: baked.destroy })
}));

import PlayerSlotRow from "$components/lobby/PlayerSlotRow.svelte";

const AVATAR_SELECTOR = ".avatar-sprite, .avatar-placeholder";
const AVATAR_GIF = /base_player\.gif|bot_animated\.gif/;
const HUMAN_STRIP = "/assets/base_player_strip.png";
const BOT_STRIP = "/assets/bot_animated_strip.png";

function backgroundOf(el: HTMLElement): string {
	return el.style.getPropertyValue("background-image");
}

describe("PlayerSlotRow avatars", () => {
	it("renders the white base strip for a human and a bot variant for a bot", () => {
		const { container } = render(PlayerSlotRow, { props: { humans: 1, bots: 1 } });

		const surfaces = Array.from(container.querySelectorAll(AVATAR_SELECTOR)) as HTMLElement[];
		expect(surfaces).toHaveLength(2);

		expect(surfaces.filter((el) => backgroundOf(el).includes(HUMAN_STRIP))).toHaveLength(1);
		expect(surfaces.filter((el) => backgroundOf(el).includes(BOT_STRIP))).toHaveLength(1);
	});

	it("never bakes the red default avatar for a human", () => {
		render(PlayerSlotRow, { props: { humans: 1, bots: 1 } });

		expect(baked.set).not.toHaveBeenCalled();
	});

	it("keeps the empty slot sprite", () => {
		const { container } = render(PlayerSlotRow, { props: { empty: 1 } });

		expect(container.querySelector('img[src="/assets/missing_player.png"]')).toBeInTheDocument();
	});

	it("references no avatar gif", () => {
		const { container } = render(PlayerSlotRow, { props: { humans: 1, bots: 1 } });

		expect(container.innerHTML).not.toMatch(AVATAR_GIF);
	});
});
