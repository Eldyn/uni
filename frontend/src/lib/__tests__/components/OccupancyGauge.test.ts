import { describe, it, expect } from "vitest";
import { render } from "@testing-library/svelte";
import OccupancyGauge from "$components/lobby/OccupancyGauge.svelte";

const AVATAR_SELECTOR = ".avatar-sprite, .avatar-placeholder";
const AVATAR_GIF = /base_player\.gif|bot_animated\.gif/;

describe("OccupancyGauge avatars", () => {
	it("renders avatars through Avatar", () => {
		const { container } = render(OccupancyGauge, { props: { filled: 2, max: 4 } });

		expect(container.querySelectorAll(AVATAR_SELECTOR)).toHaveLength(2);
	});

	it("references no avatar gif", () => {
		const { container } = render(OccupancyGauge, { props: { filled: 2, max: 4 } });

		expect(container.innerHTML).not.toMatch(AVATAR_GIF);
	});
});
