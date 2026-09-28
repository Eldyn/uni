// 4x4 ordered (Bayer) dither matrix, matching the flat, two-tone,
// no-alpha-blending dither language the rest of the app already uses
// (app.css's .dither-2/4/8) instead of a smooth gradient.

// pixelCoord is a pre-quantized pixel-grid coordinate (e.g.
// gl_FragCoord.xy / cellSize) — NOT raw fragment coordinates; dividing by
// the cell size is left to the caller so this chunk stays reusable across
// effects with different pixel grids.
float bayer4(vec2 pixelCoord) {
	vec2 cell = mod(floor(pixelCoord), 4.0);
	int index = int(cell.x) + int(cell.y) * 4;
	float bayer[16];
	bayer[0]=0.0; bayer[1]=8.0; bayer[2]=2.0; bayer[3]=10.0;
	bayer[4]=12.0; bayer[5]=4.0; bayer[6]=14.0; bayer[7]=6.0;
	bayer[8]=3.0; bayer[9]=11.0; bayer[10]=1.0; bayer[11]=9.0;
	bayer[12]=15.0; bayer[13]=7.0; bayer[14]=13.0; bayer[15]=5.0;
	for (int i = 0; i < 16; i++) {
		if (i == index) return bayer[i] / 16.0;
	}
	return 0.0;
}
