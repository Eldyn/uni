/** The mutable, Svelte-$state pose object a mounted CardFlight3D reads every
 *  frame. GSAP writes directly onto these properties each tick — because
 *  it's a $state object, that write IS the reactivity, no polling loop
 *  needed on the render side. */
export interface FlightPose {
	x: number;
	y: number;
	z: number;
	spinDeg: number;
	/** Rotation about a horizontal (table-plane) axis, for the genuine
	 *  page-turn flip — see CardMesh3D.svelte's flipRad. Independent of
	 *  spinDeg (the in-plane, world-Y rotation) since Task B2's composition
	 *  order keeps the two axes from interfering. */
	flipDeg: number;
	/** Flip axis: "x" (vertical flip / end-over-end) or "y" (horizontal flip / page-turn). */
	flipAxis?: "x" | "y";
	scale: number;
	turned: boolean;
	opacity: number;
	/** Lift factor (0 = resting in hand/pile, 1 = fully lifted on hover). */
	liftT?: number;
	/** Lateral push displacement in world units for fan spacing / hover avoidance. */
	pushX?: number;
	/** Dynamic hover punch rotation angle in degrees. */
	hoverSpinDeg?: number;
}

export function createDefaultFlightPose(overrides?: Partial<FlightPose>): FlightPose {
	return {
		x: 0,
		y: 0,
		z: 0,
		spinDeg: 0,
		flipDeg: 0,
		scale: 1,
		turned: false,
		opacity: 1,
		liftT: 0,
		pushX: 0,
		hoverSpinDeg: 0,
		...overrides
	};
}

export interface RenderContext {
	/** Look up (or lazily create) the FlightPose for a card id, seeded at a
	 *  starting pose. Renderers call this once per step to get the object
	 *  GSAP will tween. */
	getPose(cardId: string, startPose: FlightPose): FlightPose;
	/** Resolve a named world-space anchor (a pile, a hand slot, a seat) to a
	 *  [x, y, z] world position. Populated per-beat by baseBeats.ts
	 *  from the existing pure layout functions — never a DOM measurement. */
	resolveAnchor(name: string): [number, number, number];
}
