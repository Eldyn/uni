
#ifdef USE_MAP
	const float GLINT_SWEEP_PORTION = 0.6;
	const float GLINT_THICKNESS = 0.07;
	const float GLINT_RELIEF = 6.0;
	const float GLINT_SHINE = 5.0;
	const float GLINT_STRENGTH = 0.85;
	const vec3 GLINT_COLOR = vec3(1.0, 0.97, 0.86);

	float glintLumaAt(vec4 rect, vec2 pixel) {
		vec2 faceUv = clamp((pixel + 0.5) / uGlintPixels, 0.0, 1.0);
		vec2 atlasUv = rect.xy + faceUv * rect.zw;
		return dot(texture2D(map, atlasUv).rgb, vec3(0.299, 0.587, 0.114));
	}

	// Additive colour of one hard-edged diagonal line snapped to the face's own
	// art pixels, lit by the luminous relief of the face it crosses.
	vec3 glintContribution(vec4 rect, vec2 faceUv) {
		vec2 pixel = floor(faceUv * uGlintPixels);
		vec2 pixelUv = (pixel + 0.5) / uGlintPixels;

		float sweep = smoothstep(0.0, 1.0, clamp(uGlintPhase / GLINT_SWEEP_PORTION, 0.0, 1.0));
		float centre = mix(-0.30, 1.90, sweep);
		float diagonal = pixelUv.x + pixelUv.y * uGlintAspect;
		float line = 1.0 - step(GLINT_THICKNESS, abs(diagonal - centre));
		if (line <= 0.0) return vec3(0.0);

		float hC = glintLumaAt(rect, pixel);
		float hL = glintLumaAt(rect, pixel + vec2(-1.0, 0.0));
		float hR = glintLumaAt(rect, pixel + vec2( 1.0, 0.0));
		float hD = glintLumaAt(rect, pixel + vec2( 0.0, -1.0));
		float hU = glintLumaAt(rect, pixel + vec2( 0.0,  1.0));
		vec3 normal = normalize(vec3(hL - hR, hD - hU, GLINT_RELIEF));

		vec3 lightDir = normalize(vec3(-0.4, 0.55, 0.73));
		vec3 halfDir = normalize(lightDir + vec3(0.0, 0.0, 1.0));
		float spec = pow(max(dot(normal, halfDir), 0.0), GLINT_SHINE);
		float relief = mix(0.6, 1.25, spec);
		float lum = mix(0.78, 1.10, hC);

		return GLINT_COLOR * (line * relief * lum * GLINT_STRENGTH);
	}
#endif
