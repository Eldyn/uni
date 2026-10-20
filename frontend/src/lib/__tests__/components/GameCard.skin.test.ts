import { beforeEach, describe, expect, it } from "vitest";
import { fireEvent, render, screen } from "@testing-library/svelte";
import { tick } from "svelte";
import GameCard from "$components/game/GameCard.svelte";
import type { Card } from "$stores/game.svelte";
import { storeCosmetics } from "$stores/cosmetics.svelte";

const CARD = { id: 1, type: "red", value: "7" } as Card;

describe("GameCard skins", () => {
	beforeEach(() => {
		storeCosmetics.reset();
	});

	it("renders the equipped face skin", () => {
		storeCosmetics.slots = { card_face: "uni:card_face_midnight", card_back: null, playmat: null };
		render(GameCard, { props: { card: CARD } });
		expect(screen.getByTestId("game-card-face-probe").getAttribute("src")).toBe(
			"/assets/cards/skins/midnight/7.png"
		);
	});

	it("falls back to the base face when the skin file fails", async () => {
		storeCosmetics.slots = { card_face: "uni:card_face_midnight", card_back: null, playmat: null };
		render(GameCard, { props: { card: CARD } });

		await fireEvent.error(screen.getByTestId("game-card-face-probe"));
		await tick();

		expect(screen.queryByTestId("game-card-face-probe")).toBeNull();
		expect(document.querySelector(".tinted-sprite")?.getAttribute("style")).toContain(
			"/assets/cards/7.png"
		);
	});

	it("renders and falls back for the equipped back skin", async () => {
		storeCosmetics.slots = { card_face: null, card_back: "uni:card_back_stars", playmat: null };
		render(GameCard, { props: { card: CARD, turned: true } });

		const back = screen.getByTestId("game-card-back");
		expect(back.getAttribute("src")).toBe("/assets/cards/backs/stars.png");

		await fireEvent.error(back);
		await tick();

		expect(back.getAttribute("src")).toBe("/assets/cards/back.png");
	});
});
