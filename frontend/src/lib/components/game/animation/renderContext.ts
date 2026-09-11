/** The mutable, Svelte-$state pose object a mounted CardFlight3D reads every
 *  frame. GSAP writes directly onto these properties each tick — because
 *  it's a $state object, that write IS the reactivity, no polling loop
 *  needed on the render side. */
export interface FlightPose {
	x: number;
	y: number;
	z: number;
	spinDeg: number;
	scale: number;
	turned: boolean;
	opacity: number;
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
