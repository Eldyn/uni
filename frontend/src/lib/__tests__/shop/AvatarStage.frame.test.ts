import { describe, expect, it } from "vitest";

const nodeModule = (name: string) => import(/* @vite-ignore */ `node:${name}`);
const { readFileSync } = await nodeModule("fs");
const { dirname, join, resolve } = await nodeModule("path");
const { fileURLToPath } = await nodeModule("url");

const componentsRoot = resolve(dirname(fileURLToPath(import.meta.url)), "../../components");
const stageSource: string = readFileSync(join(componentsRoot, "shop/AvatarStage.svelte"), "utf8");
const wardrobeSource: string = readFileSync(
	join(componentsRoot, "wardrobe/WardrobeScreen.svelte"),
	"utf8"
);

describe("AvatarStage frame", () => {
	it("draws no purple rim frame behind the avatar", () => {
		expect(stageSource).not.toContain("c084fc");
		expect(stageSource).not.toContain("ShaderMaterial");
	});

	it("the stage box in the wardrobe has no border and no background", () => {
		const rule = wardrobeSource.match(/\.wardrobe-stage\s*\{[^}]*\}/)?.[0] ?? "";
		expect(rule).not.toBe("");
		expect(rule).not.toMatch(/border|background/);
	});

	it("has no rotation, drag or tilt code", () => {
		expect(stageSource).not.toContain("rotation.y");
		expect(stageSource).not.toContain("onpointerdown");
		expect(stageSource).not.toContain("cursor: grab");
	});
});
