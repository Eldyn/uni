/**
 * @file eventBindings.ts
 * @brief Presentation bindings: the client side of `presentations/<name>.json`
 * files of the kind `uni:binding` (format version 1). A binding names one
 * client event id and has cases. A case has matcher tests and a block of what
 * to present: animation beats, sounds, a play log line, a glossary key.
 *
 * The server sends the bindings in `defs.presentations[]`. The client matches
 * them against the event as the server PROJECTED it for this viewer. A payload
 * field that the viewer may not see is absent, so a test on it fails. The
 * `relation` test is only for bindings: it compares the actor or the target of
 * the event with the seat of the viewer.
 *
 * TEXTS: a log line carries KEYS and parameters. The chat log looks the key up
 * in the texts of its mod (`resolveLogText`, the mod text layer) and shows
 * nothing when the key has no text, so the line cannot show a half text.
 */

import type { AnimationBeat, AnimationStep } from "$components/game/animation/types";
import type { LogLine } from "$lib/chat/playLog/logEvent";
import { escapeLogText } from "$lib/chat/playLog/logRichText";
import { stableIndex } from "$lib/chat/playLog/stableIndex";

const BINDING_KEY = "uni:binding";
const PAYLOAD_TARGET_PREFIX = "$payload.";
const LOG_TIER = 0;

export interface ClientEvent {
	/** The wire type of a core event, or the full id of a mod event. */
	id: string;
	actor?: string;
	/** The per-seat sequence of the event; the log variant hash reads it. */
	sequence: number;
	/** The payload as the server projected it for this viewer. */
	payload: Record<string, unknown>;
	/** True for an event of the late join tail: it must not animate. */
	replayed: boolean;
	/** The server streak values of the envelope; absent when it has none. */
	context?: Readonly<Record<string, unknown>>;
}

export interface Viewer {
	/** The seat of the viewer; null for a spectator. */
	seat: string | null;
}

export interface EventEnvelope {
	id: string;
	actor?: string;
	sequence: number;
	payload: Record<string, unknown>;
	replayed?: boolean;
	context?: Readonly<Record<string, unknown>>;
}

type TestWord =
	"equals" | "in" | "exists" | "has_tag" | "relation" | "gt" | "gte" | "lt" | "lte" | "unknown";

export interface BindingTest {
	test: TestWord;
	field: string;
	value?: unknown;
	values: unknown[];
	tag: string;
	role: string;
	relation: string;
	negate: boolean;
	/** Streak id; when set, the test reads the streak value instead of a payload field. */
	streak: string;
	/** Literal key into an object streak value; empty when unused. */
	key: string;
	/** Payload field whose value is the key into an object streak value; empty when unused. */
	keyField: string;
	/** When true, the test reads the envelope actor instead of a field or streak. */
	actor: boolean;
}

type ParamSource =
	| { kind: "field"; field: string }
	| { kind: "value"; value: string | number }
	| { kind: "actor" }
	| { kind: "streak"; streak: string; key: string; keyField: string };

/** A param source plus the value used when the source yields nothing. */
type ParamSpec = ParamSource & { defaultValue?: string | number };

export interface SoundCue {
	sound: string;
	volume?: number;
	pitch?: number;
}

export interface BindingCase {
	tests: BindingTest[];
	beats: AnimationBeat[];
	sounds: SoundCue[];
	log: { variants: string[]; params: Record<string, ParamSpec> } | null;
	glossary: string | null;
}

export interface Binding {
	id: string;
	event: string;
	select: "all" | "first";
	cases: BindingCase[];
}

export interface PresentationOutput {
	bindingId: string;
	beats: AnimationBeat[];
	sounds: SoundCue[];
	line: LogLine | null;
	glossary: string | null;
}

function isRecord(value: unknown): value is Record<string, unknown> {
	return typeof value === "object" && value !== null && !Array.isArray(value);
}

function stringOr(value: unknown, fallback = ""): string {
	return typeof value === "string" ? value : fallback;
}

/** Turns an envelope into the event that the matcher reads. */
export function clientEventOf(envelope: EventEnvelope): ClientEvent {
	return {
		id: envelope.id,
		actor: envelope.actor,
		sequence: envelope.sequence,
		payload: envelope.payload,
		replayed: envelope.replayed === true,
		...(envelope.context === undefined ? {} : { context: envelope.context })
	};
}

const TEST_WORDS: readonly TestWord[] = [
	"equals",
	"in",
	"exists",
	"has_tag",
	"relation",
	"gt",
	"gte",
	"lt",
	"lte"
];

function parseTest(raw: unknown): BindingTest | null {
	if (!isRecord(raw) || typeof raw["uni:test"] !== "string") return null;
	const word = raw["uni:test"] as TestWord;
	return {
		test: TEST_WORDS.includes(word) ? word : "unknown",
		field: stringOr(raw["uni:field"]),
		value: raw["uni:value"],
		values: Array.isArray(raw["uni:values"]) ? raw["uni:values"] : [],
		tag: stringOr(raw["uni:tag"]),
		role: stringOr(raw["uni:role"]),
		relation: stringOr(raw["uni:relation"]),
		negate: raw["uni:negate"] === true,
		streak: stringOr(raw["uni:streak"]),
		key: stringOr(raw["uni:key"]),
		keyField: stringOr(raw["uni:key_field"]),
		actor: raw["uni:actor"] === true
	};
}

function parseStep(raw: unknown): AnimationStep | null {
	if (!isRecord(raw) || typeof raw.op !== "string" || typeof raw.target !== "string") {
		return null;
	}
	const step: AnimationStep = { op: raw.op, target: raw.target };
	if (isRecord(raw.payload)) step.payload = raw.payload;
	if (typeof raw.atS === "number") step.atS = raw.atS;
	if (typeof raw.groupId === "string") step.groupId = raw.groupId;
	return step;
}

function parseBeats(raw: unknown): AnimationBeat[] {
	if (!Array.isArray(raw)) return [];
	const beats: AnimationBeat[] = [];
	for (const rawBeat of raw) {
		if (!Array.isArray(rawBeat)) continue;
		const steps = rawBeat.map(parseStep).filter((step): step is AnimationStep => step !== null);
		if (steps.length > 0) beats.push(steps);
	}
	return beats;
}

function parseSounds(raw: unknown): SoundCue[] {
	if (!Array.isArray(raw)) return [];
	const sounds: SoundCue[] = [];
	for (const item of raw) {
		if (!isRecord(item) || typeof item["uni:sound"] !== "string") continue;
		const cue: SoundCue = { sound: item["uni:sound"] };
		if (typeof item["uni:volume"] === "number") cue.volume = item["uni:volume"];
		if (typeof item["uni:pitch"] === "number") cue.pitch = item["uni:pitch"];
		sounds.push(cue);
	}
	return sounds;
}

// The source order is safe only because the validator enforces one source.
function parseParam(raw: unknown): ParamSpec | null {
	const source = parseParamSource(raw);
	if (source === null || !isRecord(raw)) return source;
	const fallback = raw["uni:default"];
	if (typeof fallback === "string" || typeof fallback === "number") {
		return { ...source, defaultValue: fallback };
	}
	return source;
}

function parseParamSource(raw: unknown): ParamSource | null {
	if (!isRecord(raw)) return null;
	if (typeof raw["uni:field"] === "string") return { kind: "field", field: raw["uni:field"] };
	const literal = raw["uni:value"];
	if (typeof literal === "string" || typeof literal === "number") {
		return { kind: "value", value: literal };
	}
	if (raw["uni:actor"] === true) return { kind: "actor" };
	if (typeof raw["uni:streak"] === "string") {
		return {
			kind: "streak",
			streak: raw["uni:streak"],
			key: stringOr(raw["uni:key"]),
			keyField: stringOr(raw["uni:key_field"])
		};
	}
	return null;
}

function parseLog(raw: unknown): BindingCase["log"] {
	if (!isRecord(raw) || !Array.isArray(raw["uni:variants"])) return null;
	const variants = raw["uni:variants"].filter((key): key is string => typeof key === "string");
	if (variants.length === 0) return null;
	const params: Record<string, ParamSpec> = {};
	if (isRecord(raw["uni:params"])) {
		for (const [name, source] of Object.entries(raw["uni:params"])) {
			const parsed = parseParam(source);
			if (parsed) params[name] = parsed;
		}
	}
	return { variants, params };
}

function parseCase(raw: unknown): BindingCase | null {
	if (!isRecord(raw) || !isRecord(raw["uni:present"])) return null;
	const present = raw["uni:present"];
	const rawTests = raw["uni:tests"];
	const tests: BindingTest[] = [];
	if (rawTests !== undefined) {
		if (!Array.isArray(rawTests)) return null;
		for (const rawTest of rawTests) {
			const test = parseTest(rawTest);
			if (test === null) return null;
			tests.push(test);
		}
	}
	return {
		tests,
		beats: parseBeats(present["uni:beats"]),
		sounds: parseSounds(present["uni:sounds"]),
		log: parseLog(present["uni:log"]),
		glossary: typeof present["uni:glossary"] === "string" ? present["uni:glossary"] : null
	};
}

/** Reads one `defs.presentations[]` entry; null when it is no valid binding. */
export function parseBinding(raw: unknown): Binding | null {
	if (!isRecord(raw) || typeof raw.id !== "string") return null;
	const body = raw[BINDING_KEY];
	if (!isRecord(body) || typeof body["uni:event"] !== "string") return null;
	if (!Array.isArray(body["uni:cases"])) return null;
	const cases = body["uni:cases"]
		.map(parseCase)
		.filter((parsed): parsed is BindingCase => parsed !== null);
	return {
		id: raw.id,
		event: body["uni:event"],
		select: body["uni:select"] === "first" ? "first" : "all",
		cases
	};
}

/** The bindings of the active match, by event id. */
export class BindingRegistry {
	#byEvent = new Map<string, Binding[]>();
	#count = 0;

	get size(): number {
		return this.#count;
	}

	/** Replaces the bindings with those of a `defs` packet. */
	ingestDefs(payload: unknown): void {
		this.clear();
		if (!isRecord(payload) || !Array.isArray(payload.presentations)) return;
		for (const entry of payload.presentations) {
			const binding = parseBinding(entry);
			if (binding === null) continue;
			const list = this.#byEvent.get(binding.event) ?? [];
			list.push(binding);
			this.#byEvent.set(binding.event, list);
			this.#count += 1;
		}
	}

	clear(): void {
		this.#byEvent.clear();
		this.#count = 0;
	}

	forEvent(eventId: string): Binding[] {
		return this.#byEvent.get(eventId) ?? [];
	}
}

/**
 * Reads one streak value from the context of an event. An object value (a
 * per-seat count, the last wild color) needs `key` or `keyField`. A missing
 * context, streak or key gives `undefined`.
 */
function streakValueOf(event: ClientEvent, streak: string, key: string, keyField: string): unknown {
	const value = event.context?.[streak];
	if (key === "" && keyField === "") return value;
	if (!isRecord(value)) return undefined;
	const lookup = key !== "" ? key : event.payload[keyField];
	return typeof lookup === "string" && Object.hasOwn(value, lookup) ? value[lookup] : undefined;
}

function holdsRelation(test: BindingTest, event: ClientEvent, viewer: Viewer): boolean {
	const seat =
		test.role === "actor"
			? event.actor
			: test.role === "target" && typeof event.payload.target === "string"
				? event.payload.target
				: undefined;
	if (seat === undefined) return false;
	const isSelf = viewer.seat !== null && viewer.seat === seat;
	if (test.relation === "self") return isSelf;
	if (test.relation === "other") return !isSelf;
	return false;
}

function holdsTest(test: BindingTest, event: ClientEvent, viewer: Viewer): boolean {
	const value = test.actor
		? event.actor
		: test.streak !== ""
			? streakValueOf(event, test.streak, test.key, test.keyField)
			: event.payload[test.field];
	switch (test.test) {
		case "equals":
			return value !== undefined && value === test.value;
		case "in":
			return value !== undefined && test.values.includes(value);
		case "exists":
			return value !== undefined;
		case "has_tag": {
			const tags = event.payload.tags;
			return Array.isArray(tags) && tags.includes(test.tag);
		}
		case "relation":
			return holdsRelation(test, event, viewer);
		case "gt":
			return typeof value === "number" && typeof test.value === "number" && value > test.value;
		case "gte":
			return typeof value === "number" && typeof test.value === "number" && value >= test.value;
		case "lt":
			return typeof value === "number" && typeof test.value === "number" && value < test.value;
		case "lte":
			return typeof value === "number" && typeof test.value === "number" && value <= test.value;
		default:
			return false;
	}
}

function caseApplies(binding: BindingCase, event: ClientEvent, viewer: Viewer): boolean {
	return binding.tests.every((test) => {
		if (test.test === "unknown") return false;
		return holdsTest(test, event, viewer) !== test.negate;
	});
}

function resolveStepTarget(step: AnimationStep, event: ClientEvent): AnimationStep | null {
	if (!step.target.startsWith(PAYLOAD_TARGET_PREFIX)) return { ...step };
	const value = event.payload[step.target.slice(PAYLOAD_TARGET_PREFIX.length)];
	if (typeof value !== "string" && typeof value !== "number") return null;
	return { ...step, target: String(value) };
}

function resolveBeats(beats: AnimationBeat[], event: ClientEvent): AnimationBeat[] {
	const resolved: AnimationBeat[] = [];
	for (const beat of beats) {
		const steps = beat
			.map((step) => resolveStepTarget(step, event))
			.filter((step): step is AnimationStep => step !== null);
		if (steps.length > 0) resolved.push(steps);
	}
	return resolved;
}

function paramValue(source: ParamSource, event: ClientEvent): string | number | undefined {
	if (source.kind === "value") return source.value;
	if (source.kind === "actor") {
		return event.actor === undefined ? undefined : escapeLogText(event.actor);
	}
	if (source.kind === "streak") {
		const streakValue = streakValueOf(event, source.streak, source.key, source.keyField);
		if (typeof streakValue === "number") return streakValue;
		if (typeof streakValue === "string") return escapeLogText(streakValue);
		return undefined;
	}
	const value = event.payload[source.field];
	if (typeof value === "number") return value;
	if (typeof value === "string") return escapeLogText(value);
	if (typeof value === "boolean") return String(value);
	return undefined;
}

function fallbackValue(spec: ParamSpec): string | number | undefined {
	return typeof spec.defaultValue === "string"
		? escapeLogText(spec.defaultValue)
		: spec.defaultValue;
}

function buildLine(
	log: NonNullable<BindingCase["log"]>,
	bindingId: string,
	glossary: string | null,
	event: ClientEvent
): LogLine {
	const key = log.variants[stableIndex(event.sequence, bindingId, log.variants.length)]!;
	const params: Record<string, string | number> = {};
	for (const [name, source] of Object.entries(log.params)) {
		const value = paramValue(source, event) ?? fallbackValue(source);
		if (value !== undefined) params[name] = value;
	}
	if (glossary !== null) params.glossary = glossary;
	return { kind: "log", key, params, tier: LOG_TIER };
}

/**
 * Evaluates the bindings of one client event for one viewer. Every case whose
 * tests all hold gives one output, in file order. With `uni:select: "first"`
 * only the first matching case gives an output. An event with no binding
 * gives nothing.
 */
export function presentEvent(
	registry: BindingRegistry,
	event: ClientEvent,
	viewer: Viewer
): PresentationOutput[] {
	const outputs: PresentationOutput[] = [];
	for (const binding of registry.forEvent(event.id)) {
		for (const bindingCase of binding.cases) {
			if (!caseApplies(bindingCase, event, viewer)) continue;
			outputs.push({
				bindingId: binding.id,
				beats: resolveBeats(bindingCase.beats, event),
				sounds: bindingCase.sounds.map((cue) => ({ ...cue })),
				line: bindingCase.log
					? buildLine(bindingCase.log, binding.id, bindingCase.glossary, event)
					: null,
				glossary: bindingCase.glossary
			});
			if (binding.select === "first") break;
		}
	}
	return outputs;
}

/** The bindings of the match that runs now; the `defs` packet fills it. */
export const bindingRegistry = new BindingRegistry();
