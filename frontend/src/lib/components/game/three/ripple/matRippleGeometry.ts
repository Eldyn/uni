/**
 * @file matRippleGeometry.ts
 * @brief Pure conversions between board world space and the playmat mesh's
 * own UV space, for the current `MatPlacement`. `ripplePlan.ts` already owns
 * origin-in-UV and max-radius-in-UV math; this module adds the inverse (UV ->
 * world) and the UV-to-world unit scale, so the ambient dust can place
 * particles on the ripple's front in world space without re-deriving
 * Playmat3D's own placement transform.
 *
 * No Svelte state — everything here is a plain function of its arguments.
 */

import type { MatPlacement } from "../../layout/playmat";
import type { MatUv } from "./ripplePlan";

/**
 * World X/Z a `MatUv` point corresponds to on `mat`. Exact inverse of
 * `ripplePlan.ts`'s `originToMatUv` — see that function's doc for the
 * forward transform (offset, quarter turn, then the U/V axis mapping) this
 * undoes.
 */
export function matUvToWorld(uv: MatUv, mat: MatPlacement): [number, number] {
	const [width, height] = mat.size;
	const localX = (uv.u - 0.5) * width;
	const localZ = (0.5 - uv.v) * height;
	const [dx, dz] = mat.quarterTurn ? [localZ, -localX] : [localX, localZ];
	return [dx + mat.offsetX, dz + mat.offsetZ];
}

/**
 * World units spanned by one full UV unit on `mat`. `maxRadiusUv`'s radius
 * is already expressed in mat-width (U) units (aspect-corrected so it traces
 * a circle in world space), so multiplying that radius by this scale gives
 * the equivalent world-space radius.
 */
export function worldUnitsPerUv(mat: MatPlacement): number {
	return mat.size[0];
}
