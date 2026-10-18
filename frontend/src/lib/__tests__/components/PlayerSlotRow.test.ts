import { describe, it, expect } from "vitest";
import { render } from "@testing-library/svelte";
import PlayerSlotRow from "$components/lobby/PlayerSlotRow.svelte";

const AVATAR_SELECTOR = ".avatar-sprite, .avatar-placeholder";
const AVATAR_GIF = /base_player\.gif|bot_animated\.gif/;
const BOT_STRIP = "/assets/bot_animated_strip.png";

describe("PlayerSlotRow avatars", () => {
	it("renders an Avatar for a human and a bot variant for a bot", () => {
		const { container } = render(PlayerSlotRow, { props: { humans: 1, bots: 1 } });

		const surfaces = Array.from(container.querySelectorAll(AVATAR_SELECTOR)) as HTMLElement[];
		expect(surfaces).toHaveLength(2);

		const botStrips = surfaces.filter((el) =>
			el.style.getPropertyValue("background-image").includes(BOT_STRIP)
		);
		expect(botStrips).toHaveLength(1);
	});

	it("references no avatar gif", () => {
		const { container } = render(PlayerSlotRow, { props: { humans: 1, bots: 1 } });

		expect(container.innerHTML).not.toMatch(AVATAR_GIF);
	});
});
