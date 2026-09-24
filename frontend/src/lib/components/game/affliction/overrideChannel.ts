/**
 * @file overrideChannel.ts
 * @brief Read-only face-override channel backed by active `face.override`
 * afflictions.
 *
 * `CardMesh3D` / `AtlasLayerMesh` consult `override(cardId)` and, when it
 * returns a texture/uvRect, swap the face through `faceResolution.ts` /
 * `cardFaceAtlas.ts`. The channel is a thin projection of the manager's
 * composition: it never mutates state and returns `undefined` when nothing
 * applies (degrade-don't-die).
 */

import { kindTarget } from "./compose";
import type { AfflictionManager } from "./afflictionManager";

/** The face swap a renderer should apply, if any. */
export interface FaceOverride {
	uvRect?: [number, number, number, number];
	texture?: string;
}

/** Maps a raw card id to its affliction target address (`card:<id>`). A caller
 *  may inject a zone-aware resolver (T9) when a card's target is not just its
 *  own address. */
export type CardTargetResolver = (cardId: string) => string;

function defaultCardTarget(cardId: string): string {
	return cardId.includes(":") ? cardId : `card:${cardId}`;
}

function isUvRect(value: unknown): value is [number, number, number, number] {
	return (
		Array.isArray(value) &&
		value.length === 4 &&
		value.every((n) => typeof n === "number" && Number.isFinite(n))
	);
}

function toFaceOverride(value: unknown): FaceOverride | undefined {
	if (!value || typeof value !== "object") return undefined;
	const modifier = value as Record<string, unknown>;
	const out: FaceOverride = {};
	if (isUvRect(modifier.uvRect)) out.uvRect = [...modifier.uvRect];
	if (typeof modifier.texture === "string") out.texture = modifier.texture;
	return out.uvRect === undefined && out.texture === undefined ? undefined : out;
}

export class OverrideChannel {
	#mgr: AfflictionManager;
	#resolveTarget: CardTargetResolver;

	constructor(mgr: AfflictionManager, resolveTarget: CardTargetResolver = defaultCardTarget) {
		this.#mgr = mgr;
		this.#resolveTarget = resolveTarget;
	}

	/**
	 * The highest-priority active `face.override` for `cardId`, or `undefined`.
	 * An `exclusive` owner on the target suppresses face overrides, matching the
	 * composition engine.
	 */
	override(cardId: string): FaceOverride | undefined {
		const target = this.#resolveTarget(cardId);
		return toFaceOverride(this.#mgr.resolve(kindTarget("face.override", target)));
	}
}
