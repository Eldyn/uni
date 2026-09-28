// Ambient dust: motes drift on a fixed cycle derived entirely from their own
// spawn seed and uTime (quantized to 12fps by AmbientDust3D.svelte before it
// ever reaches this uniform) — no per-frame CPU-side state, no
// instanceMatrix. Each instance's aSeed.xyz is its spawn position in board
// world space, aSeed.w a random phase in [0, 1) that staggers its cycle so
// motes don't all fade in/out in lockstep.
//
// A second, independent motion layer rides on top: the analytic wild-ripple
// puff. It doesn't touch aSeed or the drift/bob math above it — it only adds
// to worldPosition.y for the handful of motes near the felt surface while a
// wild ripple's front is passing under them.
precision mediump float;

uniform float uTime;
uniform float uBlockWorldSize;
uniform vec2 uRippleOriginWorld;
uniform float uRippleStartTime;
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
// Wild puff: a lifted mote decays back to its unlifted height over this many
// seconds after the ripple front has passed it, and only lifts motes within
// this many blocks of the mat surface (worldPosition.y == 0).
const float DUST_WILD_LIFT_SECONDS = 0.8;
const float DUST_WILD_LIFT_HEIGHT_BLOCKS = 3.0;
const float DUST_WILD_LIFT_BLOCKS = 1.5;
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
	// this mote's distance from the origin — rather than an analytic
	// distance/speed crossing against continuous time. That distance/speed
	// approach used to read the front as passing one 12fps step later than
	// frontRadiusAt shows it on the playmat itself; this mirrors the same
	// step semantics so both sweeps agree on every step, not just within one
	// step's worth of quantization error. Once a mote is known to have been
	// passed, its decay back down still runs on continuous time, from that
	// step's own boundary.
	float liftHeightLimit = DUST_WILD_LIFT_HEIGHT_BLOCKS * uBlockWorldSize;
	if (uRippleWild > 0.5 && abs(basePosition.y) <= liftHeightLimit && uRippleMaxRadius > 0.0) {
		vec2 toMote = basePosition.xz - uRippleOriginWorld;
		float dist = length(toMote);
		if (dist <= uRippleMaxRadius) {
			float currentStep = floor((uTime - uRippleStartTime) / DUST_STEP_SECONDS);
			float arrivalStep = ceil(
				(uRippleTotalSteps * dist) / uRippleMaxRadius - 1.0 - DUST_STEP_ROUNDING_EPSILON
			);
			arrivalStep = clamp(arrivalStep, 0.0, uRippleTotalSteps - 1.0);
			if (currentStep >= arrivalStep) {
				float arrivalTime = uRippleStartTime + arrivalStep * DUST_STEP_SECONDS;
				float sincePassed = uTime - arrivalTime;
				if (sincePassed >= 0.0 && sincePassed <= DUST_WILD_LIFT_SECONDS) {
					float lift =
						(1.0 - sincePassed / DUST_WILD_LIFT_SECONDS) * DUST_WILD_LIFT_BLOCKS * uBlockWorldSize;
					basePosition.y += lift;
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
