/**
 * @file triggers.ts
 * @brief Client-side presentation trigger evaluator + installer.
 *
 * Presentations are mod content carried in the `defs` packet. A presentation's
 * `triggers[]` decide *when* it becomes active:
 *
 *  - **always** — `on` absent: installed at registration (match start).
 *  - **status** — `on: "status:<id>"`: installed when the server reports that
 *    status (a `status_applied` `match_event`).
 *  - **match** — `on: "<match_event type>"` (or the internal `match:` prefix):
 *    installed when that event arrives and its `where` matches.
 *
 * Conditions are deliberately minimal: `where` is a flat map of
 * scalar **equality** against the raw payload — no operators yet.
 *
 * The module is pure `.ts` with an injectable clock only through the platform
 * timers (tests use fake timers) and never touches `storeGame.state`. Unknown
 * sources/event types degrade with a warning and never throw, so a mod-authored
 * trigger cannot wedge the match (degrade-don't-die). Installs honour the
 * reduced-motion / animation toggle through the public `enabled` gate.
 */

import type { AfflictionModifier } from "./compose";
import type { AfflictionManager } from "./afflictionManager";

/** One `{ on?, where?, duration? }` trigger as serialized in a presentation def. */
export interface TriggerDef {
	/** Source selector. Absent = `always`; `status:<id>`; else a match-event type. */
	on?: string;
	/** Flat scalar equality conditions against the event payload. */
	where?: Record<string, unknown>;
	/** Seconds before the presentation expires; absent = persistent. */
	duration?: number;
}

/** A presentation def as delivered by the `defs` packet; loose by design. */
export interface PresentationDef {
	id: string;
	target: string;
	priority?: number;
	modifiers?: AfflictionModifier[];
	triggers?: TriggerDef[];
}

/** Registers a raw `match_event` subscriber and returns its unsubscriber. */
export type MatchEventSubscriber = (cb: (type: string, payload: unknown) => void) => () => void;

type TriggerSource =
	{ kind: "always" } | { kind: "status"; id: string } | { kind: "match"; type: string };

interface PendingTrigger {
	def: PresentationDef;
	trigger: TriggerDef;
}

const MATCH_PREFIX = "match:";
const STATUS_PREFIX = "status:";

function isRecord(value: unknown): value is Record<string, unknown> {
	return typeof value === "object" && value !== null && !Array.isArray(value);
}

/**
 * @brief Classifies a trigger source.
 *
 * `on` absent/empty → `always`; `status:<id>` → `status`; everything else is a
 * `match` event type, with the internal `match:` prefix stripped when present.
 */
export function sourceKind(on: string | undefined): TriggerSource {
	if (on === undefined || on === null || on === "") return { kind: "always" };
	if (on.startsWith(STATUS_PREFIX)) return { kind: "status", id: on.slice(STATUS_PREFIX.length) };
	const type = on.startsWith(MATCH_PREFIX) ? on.slice(MATCH_PREFIX.length) : on;
	return { kind: "match", type };
}

/**
 * @brief Flat scalar equality: every `where` key must equal the payload's value
 * by `===`. An empty/absent `where` is unconditional, extra payload keys are
 * ignored, and a missing key never matches (no operators yet).
 */
export function evalWhere(
	where: Record<string, unknown> | undefined,
	payload: Record<string, unknown>
): boolean {
	if (!where) return true;
	for (const key of Object.keys(where)) {
		if (payload[key] !== where[key]) return false;
	}
	return true;
}

/**
 * @class PresentationTriggers
 * @brief Holds presentation defs and installs them into an `AfflictionManager`
 * when their source fires. The manager remains the single writer of active
 * affliction state; this class only decides *when* to call `activate`/`expire`.
 */
export class PresentationTriggers {
	#mgr: AfflictionManager;
	#enabled = true;
	#eventTriggers = new Map<string, PendingTrigger[]>();
	#statusTriggers = new Map<string, PendingTrigger[]>();
	#timers = new Map<string, ReturnType<typeof setTimeout>>();

	constructor(mgr: AfflictionManager) {
		this.#mgr = mgr;
	}

	/** Reduced-motion / animation gate. When off, no presentation installs. */
	get enabled(): boolean {
		return this.#enabled;
	}

	set enabled(value: boolean) {
		this.#enabled = value;
	}

	/**
	 * @brief Registers every trigger on a presentation def, routing it to the
	 * matching source handler (the source mapping). A def with no triggers
	 * is unconditional and installs immediately.
	 */
	register(def: PresentationDef): void {
		const triggers = Array.isArray(def.triggers) ? def.triggers : [];
		if (triggers.length === 0) {
			this.onAlways(def, {});
			return;
		}
		for (const trigger of triggers) {
			const source = sourceKind(trigger.on);
			if (source.kind === "always") this.onAlways(def, trigger);
			else if (source.kind === "status") this.onStatus(def, trigger);
			else this.#registerEvent(def, trigger, source.type);
		}
	}

	/** Install a def whose `always` trigger fired. */
	onAlways(def: PresentationDef, trigger?: TriggerDef): void {
		this.#install({ def, trigger: trigger ?? this.#pickTrigger(def, "always") ?? {} });
	}

	/** Register a def whose `status:<id>` trigger should install on that status. */
	onStatus(def: PresentationDef, trigger?: TriggerDef): void {
		const resolved = trigger ?? this.#pickTrigger(def, "status");
		const source = sourceKind(resolved?.on);
		if (!resolved || source.kind !== "status" || source.id === "") {
			this.#warn("onStatus: presentation has no valid status trigger", def.id);
			return;
		}
		this.#push(this.#statusTriggers, source.id, { def, trigger: resolved });
	}

	/**
	 * Deliver one raw `match_event` `(type, payload)`. Installs every registered
	 * event trigger whose type matches and whose `where` passes, and every status
	 * trigger matching a `status_applied` payload. Unknown types are ignored.
	 */
	onEvent(type: string, payload: unknown = {}): void {
		const data = isRecord(payload) ? payload : {};
		const eventType = type.startsWith(MATCH_PREFIX) ? type.slice(MATCH_PREFIX.length) : type;

		const events = this.#eventTriggers.get(eventType);
		if (events) {
			for (const entry of events) {
				if (evalWhere(entry.trigger.where, data)) this.#install(entry);
			}
		}

		if (eventType === "status_applied") {
			const statusId = data.status_kind;
			if (typeof statusId === "string") {
				const statuses = this.#statusTriggers.get(statusId);
				if (statuses) {
					for (const entry of statuses) {
						if (evalWhere(entry.trigger.where, data)) this.#install(entry);
					}
				}
			}
		}
	}

	/** Cancel every scheduled expiry and drop every registration. Called before a
	 *  replacement `defs` set is registered and on match teardown, so a finished
	 *  match's triggers cannot fire into the next one and a repeated registration
	 *  cannot accumulate. Registrations are otherwise idempotent per `def.id`. */
	clear(): void {
		for (const handle of this.#timers.values()) clearTimeout(handle);
		this.#timers.clear();
		this.#eventTriggers.clear();
		this.#statusTriggers.clear();
	}

	/** Cancels every scheduled expiry and drops every registration. */
	dispose(): void {
		this.clear();
	}

	#registerEvent(def: PresentationDef, trigger: TriggerDef, type: string): void {
		this.#push(this.#eventTriggers, type, { def, trigger });
	}

	/** Appends `entry`, first removing any prior registration for the same
	 *  presentation def id at this source — a repeated `defs` frame replaces a
	 *  def's pending trigger instead of duplicating it. */
	#push(map: Map<string, PendingTrigger[]>, key: string, entry: PendingTrigger): void {
		const list = map.get(key);
		if (!list) {
			map.set(key, [entry]);
			return;
		}
		map.set(key, [...list.filter((existing) => existing.def.id !== entry.def.id), entry]);
	}

	#pickTrigger(def: PresentationDef, kind: TriggerSource["kind"]): TriggerDef | undefined {
		return (def.triggers ?? []).find((trigger) => sourceKind(trigger.on).kind === kind);
	}

	#install(entry: PendingTrigger): void {
		if (!this.#enabled) return;

		const { def, trigger } = entry;
		const duration =
			typeof trigger.duration === "number" &&
			Number.isFinite(trigger.duration) &&
			trigger.duration > 0
				? trigger.duration
				: null;

		this.#mgr.activate({
			presentationId: def.id,
			target: def.target,
			priority:
				typeof def.priority === "number" && Number.isFinite(def.priority) ? def.priority : 0,
			activationSeq: 0,
			modifiers: Array.isArray(def.modifiers) ? def.modifiers : [],
			expiry: duration === null ? null : Date.now() + duration * 1000
		});

		if (duration !== null) this.#scheduleExpiry(def.id, duration);
	}

	#scheduleExpiry(presentationId: string, seconds: number): void {
		const existing = this.#timers.get(presentationId);
		if (existing !== undefined) clearTimeout(existing);
		const handle = setTimeout(() => {
			this.#timers.delete(presentationId);
			this.#mgr.expire(presentationId);
		}, seconds * 1000);
		this.#timers.set(presentationId, handle);
	}

	#warn(message: string, detail?: string): void {
		if (import.meta.env.DEV) {
			console.warn(`[presentationTriggers] ${message}${detail ? `: ${detail}` : ""}`);
		}
	}
}

/**
 * @brief Bridges a raw `match_event` subscriber (the fan-out site) to a
 * `PresentationTriggers` instance. Returns the unsubscriber so a match teardown
 * can detach cleanly.
 */
export function wirePresentationTriggers(
	triggers: PresentationTriggers,
	subscribe: MatchEventSubscriber
): () => void {
	return subscribe((type, payload) => triggers.onEvent(type, payload));
}
