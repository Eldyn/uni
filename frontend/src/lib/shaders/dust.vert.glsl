// Ambient dust: motes drift on a fixed cycle derived entirely from their own
// spawn seed and uTime (continuous seconds since mount — AmbientDust3D.svelte
// feeds the raw frame clock, per the user's 2026-09-28 ruling that dust must
// not step with the 12fps pixel-look grid) — no per-frame CPU-side state, no
// instanceMatrix. Each instance's aSeed.xyz is its spawn position in board
// world space, aSeed.w a random phase in [0, 1) that staggers its cycle so
// motes don't all fade in/out in lockstep.

// A second, independent motion layer rides on top: the wild-ripple puff. It
// doesn't touch aSeed or the drift/bob math above it — near the front it only
// lifts and pushes motes in world space. The *arrival* test stays step-based
// (the ripple front it mirrors is stepped at 12fps), but the puff's decay
// rides uRippleElapsed, so the blow animates smoothly at the frame rate.
//
// highp because uTime is unbounded seconds since mount: at mediump's ~10-bit
// mantissa the fractional drift/bob motion loses all resolution after a few
// minutes, and mobile GLSL ES defaults vertex floats to mediump.
precision highp float;

uniform float uTime;
uniform float uBlockWorldSize;
uniform vec2 uRippleOriginWorld;
uniform float uRippleStep;
uniform float uRippleElapsed;
uniform float uRippleMaxRadius;
uniform float uRippleTotalSteps;
uniform float uRippleWild;

attribute vec4 aSeed;

varying float vAlpha;

const float PI = 3.14159265359;

// One full spawn -> drift -> despawn -> respawn-elsewhere cycle, in seconds.
const float DUST_CYCLE_SECONDS = 14.0;
// Fraction of the cycle spent fading in, and (mirrored) fading out.
const float DUST_FADE_FRACTION = 0.18;
// Lateral drift speed (world units/s) and a slight upward lean so the drift
// reads like slow-rising dust instead of a flat sideways crawl.
const float DUST_DRIFT_SPEED = 0.35;
const float DUST_UPWARD_LEAN = 0.08;
// Respawn offset radius (world units) — how far a mote can reappear from its
// original seed position on its next cycle, so it doesn't pop back to the
// exact same spot every 14s.
const float DUST_RESPAWN_SPREAD = 1.5;
// Per-instance bob period range, in seconds (3-6s per the brief).
const float DUST_BOB_MIN_PERIOD = 3.0;
const float DUST_BOB_MAX_PERIOD = 6.0;
const float DUST_BOB_AMPLITUDE = 0.12;
// Quad size range, in "blocks" (one felt art pixel in world units).
const float DUST_MIN_QUAD_BLOCKS = 1.0;
const float DUST_MAX_QUAD_BLOCKS = 2.0;
// Per-instance alpha range (the fragment shader multiplies this by the
// lifecycle fade written to vAlpha).
const float DUST_MIN_ALPHA = 0.12;
const float DUST_MAX_ALPHA = 0.35;
// Wild puff: motes within DUST_WILD_PROXIMITY_HEIGHT world units of the mat
// plane are eligible; a passed mote is lifted DUST_WILD_LIFT world units
// upward and pushed DUST_WILD_PUSH world units radially outward from the
// ripple origin, both decaying back to zero over DUST_WILD_LIFT_SECONDS. All
// WORLD units — the puff is a world-space displacement, not a pixel-grid
// effect, so it must never be scaled by uBlockWorldSize (one felt art pixel,
// ~0.03 world units, which shrank the old block-scaled blow to invisibility).
const float DUST_WILD_LIFT_SECONDS = 0.9;
const float DUST_WILD_PROXIMITY_HEIGHT = 2.5;
const float DUST_WILD_LIFT = 1.8;
const float DUST_WILD_PUSH = 1.3;
// The board-wide 12fps pixel-look grid (ripplePlan.ts's AMBIENT_STEP_FPS) —
// the ripple front advances in these whole steps, not continuously, so the
// puff's "has the front passed this mote" test below must reason in the same
// steps rather than an analytic distance/speed crossing.
const float DUST_STEP_SECONDS = 1.0 / 12.0;
// Same float round-trip guard as ripplePlan.ts's STEP_ROUNDING_EPSILON: a
// distance that lands exactly on a step's own radius can read a hair over it
// once this division round-trips through floating point, which would push a
// bare ceil() to the next step early.
const float DUST_STEP_ROUNDING_EPSILON = 1e-6;

// Cheap deterministic hash, seeded by a float — used to derive a handful of
// independent per-instance/per-cycle random values (drift heading, respawn
// offset, bob period, quad size, base alpha) from aSeed.w and the cycle
// index, without a texture lookup.
float hash11(float x) {
	return fract(sin(x * 127.1) * 43758.5453123);
}

void main() {
	float phase = aSeed.w;
	float cycleTime = uTime + phase * DUST_CYCLE_SECONDS;
	float cycleIndex = floor(cycleTime / DUST_CYCLE_SECONDS);
	float age = cycleTime - cycleIndex * DUST_CYCLE_SECONDS;
	float lifeFrac = age / DUST_CYCLE_SECONDS;

	// Fresh per-cycle randoms: a drift heading, a respawn offset direction
	// and magnitude, a bob period, a quad size and a base alpha — all reseed
	// together whenever cycleIndex advances, so a mote's whole look and path
	// change together on respawn instead of popping mid-attribute.
	float seedBase = phase * 991.0 + cycleIndex * 57.0;
	float driftAngle = hash11(seedBase + 1.0) * 2.0 * PI;
	float respawnAngle = hash11(seedBase + 2.0) * 2.0 * PI;
	float respawnDist = hash11(seedBase + 3.0) * DUST_RESPAWN_SPREAD;
	float bobPeriod = mix(DUST_BOB_MIN_PERIOD, DUST_BOB_MAX_PERIOD, hash11(seedBase + 4.0));
	float quadBlocks = mix(DUST_MIN_QUAD_BLOCKS, DUST_MAX_QUAD_BLOCKS, hash11(seedBase + 5.0));
	float baseAlpha = mix(DUST_MIN_ALPHA, DUST_MAX_ALPHA, hash11(seedBase + 6.0));

	vec3 respawnOffset = vec3(cos(respawnAngle), 0.0, sin(respawnAngle)) * respawnDist;
	vec3 driftDir = vec3(cos(driftAngle), DUST_UPWARD_LEAN, sin(driftAngle));
	vec3 basePosition = aSeed.xyz + respawnOffset + driftDir * DUST_DRIFT_SPEED * age;
	basePosition.y += sin((age / bobPeriod) * 2.0 * PI) * DUST_BOB_AMPLITUDE;

	// Wild ripple puff: "has the front passed this mote yet" is decided with
	// exactly ripplePlan.ts's frontArrivalStepIndex formula — the step at
	// which the stepped, clamped front radius (frontRadiusAt) first reaches
	// this mote's distance from the origin. uRippleStep is the ripple-relative
	// step, computed on the CPU with the same stepIndexAt(now - startTimeMs)
	// Playmat3D uses, rather than deriving it here from the global uTime grid
	// minus a raw start time: those two clock origins disagreed by one step
	// depending on the start phase, so the dust front could read as arriving
	// a step late (or early) relative to the playmat's own sweep. The step
	// comparison is entirely integer now, so both agree on every step. The
	// arrival test stays step-based (the front it mirrors has no sub-step
	// position), but the decay is measured from uRippleElapsed — continuous
	// seconds since the sweep started — so the lift and push animate smoothly
	// at the frame rate instead of jumping once per 12fps step.
	if (
		uRippleWild > 0.5 &&
		abs(basePosition.y) <= DUST_WILD_PROXIMITY_HEIGHT &&
		uRippleMaxRadius > 0.0
	) {
		vec2 toMote = basePosition.xz - uRippleOriginWorld;
		float dist = length(toMote);
		if (dist <= uRippleMaxRadius) {
			float arrivalStep = ceil(
				(uRippleTotalSteps * dist) / uRippleMaxRadius - 1.0 - DUST_STEP_ROUNDING_EPSILON
			);
			arrivalStep = clamp(arrivalStep, 0.0, uRippleTotalSteps - 1.0);
			if (uRippleStep >= arrivalStep) {
				float sincePassed = uRippleElapsed - arrivalStep * DUST_STEP_SECONDS;
				if (sincePassed <= DUST_WILD_LIFT_SECONDS) {
					float decay =
						1.0 - clamp(sincePassed, 0.0, DUST_WILD_LIFT_SECONDS) / DUST_WILD_LIFT_SECONDS;
					basePosition.y += DUST_WILD_LIFT * decay;
					// Outward from the origin: a blow scatters motes away, not
					// just up. Guard dist 0 (normalize would divide by a
					// zero-length vector).
					basePosition.xz += (toMote / max(dist, 1e-4)) * (DUST_WILD_PUSH * decay);
				}
			}
		}
	}

	float fadeIn = smoothstep(0.0, DUST_FADE_FRACTION, lifeFrac);
	float fadeOut = 1.0 - smoothstep(1.0 - DUST_FADE_FRACTION, 1.0, lifeFrac);
	vAlpha = baseAlpha * fadeIn * fadeOut;

	float quadSize = quadBlocks * uBlockWorldSize;

	// Billboard: camera-right/up read straight off viewMatrix's columns
	// (its rows in row-major GLSL indexing), rather than from
	// instanceMatrix — every instance shares the same quad orientation
	// facing the camera, only its world offset and size differ.
	vec3 cameraRight = vec3(viewMatrix[0].x, viewMatrix[1].x, viewMatrix[2].x);
	vec3 cameraUp = vec3(viewMatrix[0].y, viewMatrix[1].y, viewMatrix[2].y);

	vec3 worldPosition = basePosition
		+ cameraRight * position.x * quadSize
		+ cameraUp * position.y * quadSize;

	gl_Position = projectionMatrix * viewMatrix * vec4(worldPosition, 1.0);
}
