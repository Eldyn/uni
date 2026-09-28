// highp, not mediump: gl_FragCoord is in the 100s-1000s of pixels, and
// mediump's limited mantissa loses integer accuracy at that magnitude,
// which corrupted the mod(floor(fragCoord/cell)) dither-cell math into
// effectively random per-pixel noise across the whole canvas instead
// of a clean periodic Bayer tile.
precision highp float;
varying vec2 vUv;
uniform float uAspect;
uniform vec2 uResolution;
uniform float uTime;
uniform vec3 uBase;
uniform vec3 uHint;
uniform float uCentersX[__MAX_RIPPLES__];
uniform float uCentersY[__MAX_RIPPLES__];
uniform float uRadii[__MAX_RIPPLES__];
uniform float uIntensities[__MAX_RIPPLES__];
uniform float uShapes[__MAX_RIPPLES__];

// Ring half-width at birth/death (thin, still just a dithered line)
// vs. at peak-of-life (fully solid ink blot, dithered only at its
// inner/outer rim). uShapes[i] (0..1, peaked mid-lifetime) interpolates
// between the two every frame.
const float BASE_HALF_WIDTH = 0.045;
const float BLOB_EXTRA_HALF_WIDTH = 0.07;
const float EDGE_SOFT = 0.03;
const float MERGE_K = 0.35;

// How far the outline bulges off a perfect circle, and how much the
// line's thickness itself varies around that outline — both as a
// fraction of the ripple's own size, so small and large ripples wobble
// proportionally instead of the same blob looking chunky when tiny.
const float RADIUS_WOBBLE = 0.35;
const float THICKNESS_WOBBLE = 0.6;

// Cheap organic wobble: three sine harmonics at incommensurate
// frequencies around the angle, seeded per-ripple so blobs don't all
// wobble in lockstep, and rotating slowly with uTime so the outline
// writhes instead of holding a static bulge pattern.
float wobble(float angle, float seed) {
	return sin(angle * 3.0 + seed) * 0.5
		+ sin(angle * 5.0 + seed * 1.7 + 1.3) * 0.3
		+ sin(angle * 7.0 + seed * 2.3 + 2.6) * 0.2;
}

// Pixel grid the whole effect is quantized to — both the ripple
// geometry AND the dither matrix sample this same grid (matching the
// codrops dithering-shader approach of tying the Bayer lookup to the
// same pixelation cell as the pixelated coordinates), so there's one
// consistent block size instead of two competing scales fighting
// each other.
const float GRID_COLUMNS = 180.0;

// Polynomial smooth-min (Quilez) — blends two values instead of hard
// min/max, so overlapping ripples thicken into each other like SDF
// metaballs merging rather than one ring just occluding the other.
float smin(float a, float b, float k) {
	float h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);
	return mix(b, a, h) - k * h * (1.0 - h);
}

float smax(float a, float b, float k) {
	return -smin(-a, -b, k);
}

void main() {
	float strength = 0.0;

	// Square cells sized off the horizontal resolution only, so the
	// grid stays square regardless of aspect — this is what makes the
	// ripple geometry itself blocky pixel art, not just an
	// anti-aliased curve with dither painted over it.
	vec2 cellSize = vec2(uResolution.x / GRID_COLUMNS);
	vec2 pixelUv = (floor(gl_FragCoord.xy / cellSize) * cellSize + cellSize * 0.5) / uResolution;

	for (int i = 0; i < __MAX_RIPPLES__; i++) {
		vec2 center = vec2(uCentersX[i], uCentersY[i]);
		vec2 d = (pixelUv - center) * vec2(uAspect, 1.0);
		float dist = length(d);
		float radius = uRadii[i];

		// Per-ripple seed from its (fixed) center, so each ripple gets
		// its own bulge pattern instead of every one wobbling in sync.
		float seed = fract(sin(dot(center, vec2(12.9898, 78.233))) * 43758.5453) * 6.2831853;
		float angle = atan(d.y, d.x);
		float radiusNoise = wobble(angle, seed + uTime * 0.6);
		float thicknessNoise = wobble(angle * 1.3 + 10.0, seed * 1.9 - uTime * 0.4);

		float wobbleRadius = radius + radiusNoise * RADIUS_WOBBLE * max(radius, 0.05);
		float halfWidth = (BASE_HALF_WIDTH + uShapes[i] * BLOB_EXTRA_HALF_WIDTH)
			* max(0.15, 1.0 + thicknessNoise * THICKNESS_WOBBLE);
		float d2 = abs(dist - wobbleRadius);
		float band = (1.0 - smoothstep(halfWidth - EDGE_SOFT, halfWidth, d2)) * uIntensities[i];

		// Guard: smooth min/max overshoots near-equal inputs by up to
		// k/4 even at (0,0) — without this every empty patch of screen
		// got nudged to a non-zero strength, dithering the whole
		// background instead of just where ripples actually reach.
		if (strength > 0.001 && band > 0.001) {
			strength = smax(strength, band, MERGE_K);
		} else {
			strength = max(strength, band);
		}
	}

	// step(edge, x) fires on x >= edge, and the Bayer matrix includes a
	// 0.0 cell — without the epsilon, that one cell in 16 draws the hint
	// color even at strength 0.0, dithering the ENTIRE background at
	// every idle moment instead of only inside a ripple's ring.
	float threshold = bayer4(gl_FragCoord.xy / cellSize) + 0.0001;
	float draw = step(threshold, strength);
	gl_FragColor = vec4(mix(uBase, uHint, draw), 1.0);
}
