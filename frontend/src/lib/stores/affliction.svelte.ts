/**
 * @file affliction.svelte.ts
 * @brief Reactive owner of the match's affliction subsystem.
 *
 * Wraps the pure `AfflictionManager` + `PresentationTriggers` with the two seams
 * a live match needs:
 *
 *  - **Reactivity.** `AfflictionManager` is plain TS, so this store subclasses it
 *    to bump a `$state` revision on every activation/expiry. `override()` reads
 *    that revision, so `CardMesh3D`'s `$derived` face lookup recomputes when a
 *    presentation installs or expires.
 *  - **defs bootstrap (ruling 2).** `bind()` subscribes to the raw `match_event`
 *    fan-out, defines `defs.entities[]` in the shared `EntityRuntime`, and
 *    registers `defs.presentations[]` through `PresentationTriggers`. Disposal
 *    cancels pending expiry timers and detaches the subscription.
 *
 * The cosmetic RNG seed is per-client and persisted locally (never in
 * `storeGame.state`), following the audio-store persistence shape.
 */

import { AfflictionManager } from "$components/game/affliction/afflictionManager";
import { OverrideChannel, type FaceOverride } from "$components/game/affliction/overrideChannel";
import {
	PresentationTriggers,
	wirePresentationTriggers,
	type MatchEventSubscriber,
	type PresentationDef
} from "$components/game/affliction/triggers";
import { CosmeticRng } from "$components/game/affliction/cosmeticRng";
import { STANDARD_TILE_VALUES } from "$components/game/three/cardFaceAtlas";
import { EntityRuntime } from "$components/game/entity/entityRuntime.svelte";

const COSMETIC_SEED_KEY = "uni:fx:cosmetic-seed";

function isRecord(value: unknown): value is Record<string, unknown> {
	return typeof value === "object" && value !== null && !Array.isArray(value);
}

/**
 * @brief The per-client cosmetic seed. Persisted so a client's cosmetic
 * randomness is stable across reloads; generated from `crypto.randomUUID`
 * (falling back to a timestamp) on first use. Never enters game state.
 */
export function cosmeticSeed(): string {
	try {
		const stored = localStorage.getItem(COSMETIC_SEED_KEY);
		if (stored) return stored;
		const fresh = globalThis.crypto?.randomUUID?.() ?? `uni-fx-${Date.now()}-${Math.random()}`;
		localStorage.setItem(COSMETIC_SEED_KEY, fresh);
		return fresh;
	} catch {
		return "uni-fx-default-seed";
	}
}

/** `AfflictionManager` with a reactive revision for renderer reactivity. */
class ReactiveAfflictionManager extends AfflictionManager {
	revision = $state(0);

	override activate(a: Parameters<AfflictionManager["activate"]>[0]): void {
		super.activate(a);
		this.revision += 1;
	}

	override expire(presentationId: string): void {
		super.expire(presentationId);
		this.revision += 1;
	}
}

export class AfflictionStore {
	#manager = new ReactiveAfflictionManager();
	#triggers = new PresentationTriggers(this.#manager);
	#rng = new CosmeticRng(cosmeticSeed());
	#channel = new OverrideChannel(this.#manager, undefined, (source) => this.#resolveSource(source));
	#unsubscribe: (() => void) | null = null;

	get manager(): AfflictionManager {
		return this.#manager;
	}

	get triggers(): PresentationTriggers {
		return this.#triggers;
	}

	get channel(): OverrideChannel {
		return this.#channel;
	}

	/** Reduced-motion / animation gate (T8 `PresentationTriggers.enabled`). */
	get enabled(): boolean {
		return this.#triggers.enabled;
	}

	set enabled(value: boolean) {
		this.#triggers.enabled = value;
	}

	/**
	 * @brief The active face override for a card, or `undefined`. Reading the
	 * manager's revision makes this reactive, so renderer `$derived`s recompute
	 * when an affliction installs or expires.
	 */
	override(cardId: string): FaceOverride | undefined {
		void this.#manager.revision;
		return this.#channel.override(cardId);
	}

	/**
	 * @brief Subscribes to the raw `match_event` fan-out: `defs` frames bootstrap
	 * entities/presentations, every frame advances the trigger evaluator.
	 * @returns A disposer (idempotent).
	 */
	bind(runtime: EntityRuntime, subscribe: MatchEventSubscriber): () => void {
		this.dispose();
		this.#unsubscribe = wirePresentationTriggers(this.#triggers, (cb) =>
			subscribe((type, payload) => {
				if (type === "defs") this.ingestDefs(payload, runtime);
				cb(type, payload);
			})
		);
		return () => this.dispose();
	}

	/** Defines `defs.entities[]` and registers `defs.presentations[]`. */
	ingestDefs(payload: unknown, runtime?: EntityRuntime): void {
		if (!isRecord(payload)) return;
		if (Array.isArray(payload.entities) && runtime) {
			for (const entity of payload.entities) this.#defineEntity(runtime, entity);
		}
		if (Array.isArray(payload.presentations)) {
			for (const presentation of payload.presentations) {
				this.#registerPresentation(presentation);
			}
		}
	}

	/** Cancels pending expiry timers and detaches the match-event subscription. */
	dispose(): void {
		this.#triggers.dispose();
		this.#unsubscribe?.();
		this.#unsubscribe = null;
	}

	#resolveSource(source: string): FaceOverride | undefined {
		if (source !== "seeded-random-tile") return undefined;
		const tile = this.#rng.pick([...STANDARD_TILE_VALUES]);
		return tile === undefined ? undefined : { texture: tile };
	}

	#defineEntity(runtime: EntityRuntime, raw: unknown): void {
		if (!isRecord(raw) || typeof raw.id !== "string") return;
		const components = isRecord(raw.components) ? raw.components : {};
		runtime.define(raw.id, { components });
	}

	#registerPresentation(raw: unknown): void {
		if (!isRecord(raw) || typeof raw.id !== "string" || typeof raw.target !== "string") return;
		const def: PresentationDef = {
			id: raw.id,
			target: raw.target,
			priority: typeof raw.priority === "number" ? raw.priority : 0,
			modifiers: Array.isArray(raw.modifiers)
				? (raw.modifiers as PresentationDef["modifiers"])
				: [],
			triggers: Array.isArray(raw.triggers) ? (raw.triggers as PresentationDef["triggers"]) : []
		};
		try {
			this.#triggers.register(def);
		} catch (err) {
			console.warn(`[affliction] presentation "${raw.id}" failed to register — dropped.`, err);
		}
	}
}

export const storeAffliction = new AfflictionStore();
