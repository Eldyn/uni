
#ifdef USE_MAP
	vec4 rect = gl_FrontFacing ? uUvRectFront : uUvRectBack;
	vec2 baseUv = vec2(gl_FrontFacing ? vMapUv.x : (1.0 - vMapUv.x), vMapUv.y);
	vec2 atlasUv = rect.xy + baseUv * rect.zw;
	vec4 sampledDiffuseColor = texture2D( map, atlasUv );
	#ifdef DECODE_VIDEO_TEXTURE
		sampledDiffuseColor = sRGBTransferEOTF( sampledDiffuseColor );
	#endif
	diffuseColor *= sampledDiffuseColor;
	if (gl_FrontFacing && uGlintStrength > 0.0) {
		diffuseColor.rgb += glintContribution(rect, baseUv) * uGlintStrength;
	}
#endif
