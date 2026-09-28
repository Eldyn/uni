/**
 * @file renderSettings.svelte.ts
 * @brief Reactive store owning graphics and card rendering settings with localStorage persistence.
 */
import { storeWebglCapability } from "./webglCapability.svelte";
import { storeAnimation } from "./animation.svelte";

export const SETTINGS_STORAGE_KEY = "uni:render:settings";

export interface RenderSettingsState {
	cardRenderMode: "legacy" | "atlas" | "instanced";
	drawPileThickness: "full" | "capped";
	clickToPlay: boolean;
	syncCursorOnClick: boolean;
	autoScrollOnEdgeCreep: boolean;
	matRipple: boolean;
	ambientDust: boolean;
}

export class RenderSettings {
	#cardRenderMode = $state<"legacy" | "atlas" | "instanced">("atlas");
	#drawPileThickness = $state<"full" | "capped">(
		typeof window !== "undefined" && storeWebglCapability.deviceTier === "low" ? "capped" : "full"
	);
	#clickToPlay = $state<boolean>(true);
	#syncCursorOnClick = $state<boolean>(true);
	#autoScrollOnEdgeCreep = $state<boolean>(true);
	#matRipple = $state<boolean>(true);
	#ambientDust = $state<boolean>(
		typeof window !== "undefined" && storeWebglCapability.deviceTier === "high"
	);

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
				if (typeof parsed.clickToPlay === "boolean") {
					this.#clickToPlay = parsed.clickToPlay;
				}
				if (typeof parsed.syncCursorOnClick === "boolean") {
					this.#syncCursorOnClick = parsed.syncCursorOnClick;
				}
				if (typeof parsed.autoScrollOnEdgeCreep === "boolean") {
					this.#autoScrollOnEdgeCreep = parsed.autoScrollOnEdgeCreep;
				}
				if (typeof parsed.matRipple === "boolean") {
					this.#matRipple = parsed.matRipple;
				}
				if (typeof parsed.ambientDust === "boolean") {
					this.#ambientDust = parsed.ambientDust;
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

	get clickToPlay(): boolean {
		return this.#clickToPlay;
	}

	set clickToPlay(v: boolean) {
		this.#clickToPlay = v;
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

	get matRipple(): boolean {
		return this.#matRipple;
	}

	set matRipple(v: boolean) {
		this.#matRipple = v;
		this.#persist();
	}

	get ambientDust(): boolean {
		return this.#ambientDust;
	}

	set ambientDust(v: boolean) {
		this.#ambientDust = v;
		this.#persist();
	}

	/** Combines the saved toggle with the reduced-motion and animation-enabled
	 *  runtime gates. Reduced motion never mutates `#matRipple` itself. */
	get matRippleActive(): boolean {
		return this.#matRipple && !storeWebglCapability.reducedMotion && storeAnimation.enabled;
	}

	/** Combines the saved toggle with the reduced-motion and animation-enabled
	 *  runtime gates. Reduced motion never mutates `#ambientDust` itself. */
	get ambientDustActive(): boolean {
		return this.#ambientDust && !storeWebglCapability.reducedMotion && storeAnimation.enabled;
	}

	#persist(): void {
		try {
			if (typeof localStorage === "undefined") return;
			const data: RenderSettingsState = {
				cardRenderMode: this.#cardRenderMode,
				drawPileThickness: this.#drawPileThickness,
				clickToPlay: this.#clickToPlay,
				syncCursorOnClick: this.#syncCursorOnClick,
				autoScrollOnEdgeCreep: this.#autoScrollOnEdgeCreep,
				matRipple: this.#matRipple,
				ambientDust: this.#ambientDust
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

	setClickToPlay(v: boolean): void {
		this.clickToPlay = v;
	}

	setSyncCursorOnClick(sync: boolean): void {
		this.syncCursorOnClick = sync;
	}

	setAutoScrollOnEdgeCreep(autoScroll: boolean): void {
		this.autoScrollOnEdgeCreep = autoScroll;
	}

	setMatRipple(v: boolean): void {
		this.matRipple = v;
	}

	setAmbientDust(v: boolean): void {
		this.ambientDust = v;
	}
}

export const storeRenderSettings = new RenderSettings();
