// Shared by everything painted on the mat sheet (the felt and the direction
// loop): the flat, two-tone, Bayer-dithered colour sweep from uFromColor to
// uToColor. When the ripple is inactive the component sets uFromColor ==
// uToColor == the committed colour and uActive = 0, so every branch below
// collapses back to a flat tint*map output identical to the old material's.
uniform vec3 uFromColor;
uniform vec3 uToColor;
uniform vec2 uOriginUv;
uniform float uRadius;
uniform float uBandBlocks;
uniform vec2 uBlockCount;
uniform float uAspect;
uniform float uFlash;
uniform float uActive;

// Wild plays flash the origin block itself before the sweep is visibly under
// way (ripplePlan.ts's WILD_FLASH_STEPS/WILD_FLASH_RADIUS_BLOCKS) — kept in
// sync with that constant via a build-time string replace (see Playmat3D.svelte),
// the same way rippleQuad.frag.glsl's __MAX_RIPPLES__ is resolved.
const float FLASH_RADIUS_BLOCKS = float(__WILD_FLASH_RADIUS_BLOCKS__);
const float FLASH_LIGHTEN = 0.65;

vec3 matRippleColor(vec2 uv) {
	// One block = one art pixel (uBlockCount is the felt's art-pixel grid
	// size, not its raw texel size — see FELT_TEXELS_PER_ART_PIXEL in
	// Playmat3D.svelte), so the ripple's dithered edge reads as part of the
	// same pixel grid as the painted felt instead of a smooth vector curve
	// laid over it. Distance (dist, below) is measured in U units, so
	// blockSize must be in U units too: dividing by uBlockCount.x alone
	// (not max(x, y)) is what keeps the two consistent.
	float blockSize = 1.0 / uBlockCount.x;
	vec2 blockUv = (floor(uv * uBlockCount) + 0.5) / uBlockCount;

	vec2 delta = blockUv - uOriginUv;
	float dist = length(vec2(delta.x, delta.y * uAspect));

	vec3 color = uToColor;

	if (uActive > 0.5) {
		float bandWidth = max(uBandBlocks, 1.0) * blockSize;

		if (dist >= uRadius) {
			color = uFromColor;
		} else if (dist >= uRadius - bandWidth) {
			// Position across the band: 0 at its outer edge (still the old
			// colour), 1 at its inner edge (fully the new one). The Bayer
			// threshold picks one or the other per block instead of
			// blending them, matching the flat two-tone dither look the
			// rest of the app uses (app.css's .dither-2/4/8).
			float bandPos = (uRadius - dist) / bandWidth;
			float threshold = bayer4(blockUv * uBlockCount);
			color = bandPos >= threshold ? uToColor : uFromColor;
		}

		if (uFlash > 0.0 && dist <= FLASH_RADIUS_BLOCKS * blockSize) {
			color = mix(uToColor, vec3(1.0), uFlash * FLASH_LIGHTEN);
		}
	}

	return color;
}
