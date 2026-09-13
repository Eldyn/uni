import { getContext, setContext } from "svelte";
import { useViewport } from "./layout/useViewport.svelte";
import type { ViewportInfo } from "./layout/seatLayout";

/** Coarse viewport bucket, driving whether the HUD/turn-order strip default
 *  to expanded or collapsed and whether the mobile rail layout is active. */
export type ViewportClass = "desktop" | "tablet" | "mobile";

const MOBILE_MAX_WIDTH = 640;
const TABLET_MAX_WIDTH = 1024;

function classify(viewport: ViewportInfo): ViewportClass {
	if (viewport.width <= MOBILE_MAX_WIDTH) return "mobile";
	if (viewport.width <= TABLET_MAX_WIDTH) return "tablet";
	return "desktop";
}

import type { SceneGeometry } from "./layout/sceneGeometry";

export interface GameLayoutContext {
	readonly viewport: ViewportInfo;
	readonly viewportClass: ViewportClass;
	geometry: SceneGeometry | null;
}

const GAME_LAYOUT_KEY = Symbol("game-layout");

/** Call once from GameBoard.svelte's top level, mirroring `createCardBus`. */
export function createGameLayoutContext(): GameLayoutContext {
	const viewport = useViewport();
	let geometry = $state<SceneGeometry | null>(null);
	const ctx: GameLayoutContext = {
		get viewport() {
			return viewport;
		},
		get viewportClass() {
			return classify(viewport);
		},
		get geometry() {
			return geometry;
		},
		set geometry(g: SceneGeometry | null) {
			geometry = g;
		}
	};
	setContext(GAME_LAYOUT_KEY, ctx);
	return ctx;
}

export function useGameLayoutContext(): GameLayoutContext | undefined {
	return getContext<GameLayoutContext>(GAME_LAYOUT_KEY);
}
