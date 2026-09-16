/**
 * @file renderSettings.svelte.ts
 * @brief Reactive store owning graphics and card rendering settings with localStorage persistence.
 */
import { storeWebglCapability } from "./webglCapability.svelte";

export const SETTINGS_STORAGE_KEY = "uni:render:settings";

export interface RenderSettingsState {
	cardRenderMode: "legacy" | "atlas" | "instanced";
	drawPileThickness: "full" | "capped";
	handEdgeEffect: "vignette" | "fadeOverlay" | "none";
	syncCursorOnClick: boolean;
	autoScrollOnEdgeCreep: boolean;
}

export class RenderSettings {
	#cardRenderMode = $state<"legacy" | "atlas" | "instanced">("atlas");
	#drawPileThickness = $state<"full" | "capped">(
		typeof window !== "undefined" && storeWebglCapability.deviceTier === "low" ? "capped" : "full"
	);
	#handEdgeEffect = $state<"vignette" | "fadeOverlay" | "none">("vignette");
	#syncCursorOnClick = $state<boolean>(true);
	#autoScrollOnEdgeCreep = $state<boolean>(true);

	constructor() {
		try {
			const raw =
				typeof localStorage !== "undefined" ? localStorage.getItem(SETTINGS_STORAGE_KEY) : null;
			if (raw) {
				const parsed = JSON.parse(raw) as Partial<RenderSettingsState>;
				if (
					parsed.cardRenderMode === "legacy" ||
					parsed.cardRenderMode === "atlas" ||
					parsed.cardRenderMode === "instanced"
				) {
					this.#cardRenderMode = parsed.cardRenderMode;
				}
				if (parsed.drawPileThickness === "full" || parsed.drawPileThickness === "capped") {
					this.#drawPileThickness = parsed.drawPileThickness;
				}
				if (
					parsed.handEdgeEffect === "vignette" ||
					parsed.handEdgeEffect === "fadeOverlay" ||
					parsed.handEdgeEffect === "none"
				) {
					this.#handEdgeEffect = parsed.handEdgeEffect;
				}
				if (typeof parsed.syncCursorOnClick === "boolean") {
					this.#syncCursorOnClick = parsed.syncCursorOnClick;
				}
				if (typeof parsed.autoScrollOnEdgeCreep === "boolean") {
					this.#autoScrollOnEdgeCreep = parsed.autoScrollOnEdgeCreep;
				}
			}
		} catch {
			// localStorage unavailable or malformed
		}
	}

	get cardRenderMode(): "legacy" | "atlas" | "instanced" {
		return this.#cardRenderMode;
	}

	set cardRenderMode(mode: "legacy" | "atlas" | "instanced") {
		this.#cardRenderMode = mode;
		this.#persist();
	}

	get drawPileThickness(): "full" | "capped" {
		return this.#drawPileThickness;
	}

	set drawPileThickness(thickness: "full" | "capped") {
		this.#drawPileThickness = thickness;
		this.#persist();
	}

	get handEdgeEffect(): "vignette" | "fadeOverlay" | "none" {
		return this.#handEdgeEffect;
	}

	set handEdgeEffect(effect: "vignette" | "fadeOverlay" | "none") {
		this.#handEdgeEffect = effect;
		this.#persist();
	}

	get syncCursorOnClick(): boolean {
		return this.#syncCursorOnClick;
	}

	set syncCursorOnClick(sync: boolean) {
		this.#syncCursorOnClick = sync;
		this.#persist();
	}

	get autoScrollOnEdgeCreep(): boolean {
		return this.#autoScrollOnEdgeCreep;
	}

	set autoScrollOnEdgeCreep(autoScroll: boolean) {
		this.#autoScrollOnEdgeCreep = autoScroll;
		this.#persist();
	}

	#persist(): void {
		try {
			if (typeof localStorage === "undefined") return;
			const data: RenderSettingsState = {
				cardRenderMode: this.#cardRenderMode,
				drawPileThickness: this.#drawPileThickness,
				handEdgeEffect: this.#handEdgeEffect,
				syncCursorOnClick: this.#syncCursorOnClick,
				autoScrollOnEdgeCreep: this.#autoScrollOnEdgeCreep
			};
			localStorage.setItem(SETTINGS_STORAGE_KEY, JSON.stringify(data));
		} catch {
			// ignore storage quota errors
		}
	}

	setCardRenderMode(mode: "legacy" | "atlas" | "instanced"): void {
		this.cardRenderMode = mode;
	}

	setDrawPileThickness(thickness: "full" | "capped"): void {
		this.drawPileThickness = thickness;
	}

	setHandEdgeEffect(effect: "vignette" | "fadeOverlay" | "none"): void {
		this.handEdgeEffect = effect;
	}

	setSyncCursorOnClick(sync: boolean): void {
		this.syncCursorOnClick = sync;
	}

	setAutoScrollOnEdgeCreep(autoScroll: boolean): void {
		this.autoScrollOnEdgeCreep = autoScroll;
	}
}

export const storeRenderSettings = new RenderSettings();
