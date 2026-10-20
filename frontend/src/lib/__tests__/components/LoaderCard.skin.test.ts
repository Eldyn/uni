import { beforeEach, describe, expect, it } from "vitest";
import { fireEvent, render, screen } from "@testing-library/svelte";
import { tick } from "svelte";
import LoaderCard from "$components/common/loader/LoaderCard.svelte";
import { storeCosmetics } from "$stores/cosmetics.svelte";

const FACE = { type: "red", value: "7" } as const;

describe("LoaderCard skins", () => {
	beforeEach(() => {
		storeCosmetics.reset();
	});

	it("renders the base face with no equipped skin", () => {
		render(LoaderCard, { props: { face: { ...FACE } } });
		const sprite = document.querySelector(".tinted-sprite");
		expect(sprite?.getAttribute("style")).toContain("/assets/cards/7.png");
		expect(screen.queryByTestId("loader-card-face-probe")).toBeNull();
	});

	it("renders the equipped face skin", () => {
		storeCosmetics.slots = { card_face: "uni:card_face_midnight", card_back: null, playmat: null };
		render(LoaderCard, { props: { face: { ...FACE } } });
		expect(screen.getByTestId("loader-card-face-probe").getAttribute("src")).toBe(
			"/assets/cards/skins/midnight/7.png"
		);
		expect(document.querySelector(".tinted-sprite")?.getAttribute("style")).toContain(
			"/assets/cards/skins/midnight/7.png"
		);
	});

	it("falls back to the base face when the skin file fails", async () => {
		storeCosmetics.slots = { card_face: "uni:card_face_midnight", card_back: null, playmat: null };
		render(LoaderCard, { props: { face: { ...FACE } } });

		await fireEvent.error(screen.getByTestId("loader-card-face-probe"));
		await tick();

		expect(screen.queryByTestId("loader-card-face-probe")).toBeNull();
		expect(document.querySelector(".tinted-sprite")?.getAttribute("style")).toContain(
			"/assets/cards/7.png"
		);
	});

	it("renders the equipped back skin", () => {
		storeCosmetics.slots = { card_face: null, card_back: "uni:card_back_stars", playmat: null };
		render(LoaderCard, { props: { face: { ...FACE } } });
		expect(screen.getByTestId("loader-card-back").getAttribute("src")).toBe(
			"/assets/cards/backs/stars.png"
		);
	});

	it("falls back to the base back when the skin file fails", async () => {
		storeCosmetics.slots = { card_face: null, card_back: "uni:card_back_stars", playmat: null };
		render(LoaderCard, { props: { face: { ...FACE } } });

		await fireEvent.error(screen.getByTestId("loader-card-back"));
		await tick();

		expect(screen.getByTestId("loader-card-back").getAttribute("src")).toBe(
			"/assets/cards/back.png"
		);
	});
});
