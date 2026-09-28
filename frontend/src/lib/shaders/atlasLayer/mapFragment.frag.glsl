
#ifdef USE_MAP
	vec2 atlasUv = uUvRect.xy + vMapUv * uUvRect.zw;
	vec4 sampledDiffuseColor = texture2D( map, atlasUv );
	diffuseColor *= sampledDiffuseColor;
#endif
