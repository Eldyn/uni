#!/usr/bin/env node
/**
 * Reads contract/asyncapi.yaml and writes frontend/src/lib/generated/schemas.ts.
 *
 * Generates:
 *  - Validation constants (USERNAME_MIN, USERNAME_MAX, etc.) from constraint schemas
 *  - Zod schemas for every client → server payload and for constraint types
 *  - TypeScript types inferred from those schemas
 *  - ClientAction / ServerAction string constant objects
 *  - Per-action outgoing message schemas (payload + action literal) for runtime validation
 *
 * Usage: node scripts/generate-schemas.js
 */

import { readFileSync, writeFileSync, mkdirSync } from "fs";
import { resolve, dirname } from "path";
import { fileURLToPath } from "url";
import yaml from "js-yaml";

const __dirname = dirname(fileURLToPath(import.meta.url));
const CONTRACT = resolve(__dirname, "../../contract/asyncapi.yaml");
const OUTPUT = resolve(__dirname, "../src/lib/generated/schemas.ts");

const data = yaml.load(readFileSync(CONTRACT, "utf8"));
const schemas = data?.components?.schemas ?? {};
const messages = data?.components?.messages ?? {};
const channels = data?.channels ?? {};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/** 'game_play_card' → 'GamePlayCard' */
function toPascal(s) {
	return s
		.split(/[_/-]/)
		.map((p) => p.charAt(0).toUpperCase() + p.slice(1))
		.join("");
}

/** 'UsernameMin' → 'USERNAME_MIN' */
function pascalToUpperSnake(s) {
	return s
		.replace(/([a-z0-9])([A-Z])/g, "$1_$2")
		.replace(/([A-Z]+)([A-Z][a-z])/g, "$1_$2")
		.toUpperCase();
}

/**
 * Recursively collect every `x-constants` annotation into `out`
 * (UPPER_SNAKE name → value). First occurrence wins, so callers control
 * emission order by the order they seed the walk.
 */
function collectConstants(node, out) {
	if (Array.isArray(node)) {
		for (const item of node) collectConstants(item, out);
		return;
	}
	if (!node || typeof node !== "object") return;

	const annotations = node["x-constants"];
	if (annotations && typeof annotations === "object") {
		for (const [keyword, baseName] of Object.entries(annotations)) {
			const name = pascalToUpperSnake(baseName);
			if (node[keyword] !== undefined && !(name in out)) {
				out[name] = node[keyword];
			}
		}
	}
	for (const [key, value] of Object.entries(node)) {
		if (key === "x-constants") continue;
		collectConstants(value, out);
	}
}

/** Collect client/server action strings from channels. */
function collectActions() {
	const client = {};
	const server = {};
	for (const ch of Object.values(channels)) {
		for (const ref of ch?.publish?.message?.oneOf ?? []) {
			const name = ref.$ref?.split("/").pop();
			const actionStr = messages[name]?.name;
			if (actionStr) client[toPascal(actionStr)] = actionStr;
		}
		for (const ref of ch?.subscribe?.message?.oneOf ?? []) {
			const name = ref.$ref?.split("/").pop();
			const actionStr = messages[name]?.name;
			if (actionStr) server[toPascal(actionStr)] = actionStr;
		}
	}
	return { client, server };
}

/**
 * Resolve a property that may be a $ref to a named constraint schema.
 * Returns the resolved schema object.
 */
function resolveRef(prop) {
	if (!prop.$ref) return prop;
	const name = prop.$ref.split("/").pop();
	return schemas[name] ?? prop;
}

/** Map a JSON Schema property to a Zod chain string (without 'z.' prefix call). */
function propToZod(prop) {
	const resolved = resolveRef(prop);
	const t = resolved.type ?? "string";

	let chain = "";
	if (t === "string") {
		chain = "z.string()";
		if (resolved.minLength !== undefined) chain += `.min(${resolved.minLength})`;
		if (resolved.maxLength !== undefined) chain += `.max(${resolved.maxLength})`;
		if (resolved.pattern) chain += `.regex(/${resolved.pattern}/)`;
	} else if (t === "integer" || t === "number") {
		chain = "z.number().int()";
		if (resolved.minimum !== undefined) chain += `.min(${resolved.minimum})`;
		if (resolved.maximum !== undefined) chain += `.max(${resolved.maximum})`;
	} else if (t === "boolean") {
		chain = "z.boolean()";
	} else if (t === "array") {
		const items = resolved.items ? propToZod(resolved.items) : "z.unknown()";
		chain = `z.array(${items})`;
		if (resolved.maxItems !== undefined) chain += `.max(${resolved.maxItems})`;
	} else if (t === "object") {
		chain = "z.looseObject({})";
	} else {
		chain = "z.unknown()";
	}
	return chain;
}

/** Generate a z.object({...}) string from a schema definition. */
function schemaToZod(schemaName, schema) {
	const props = schema.properties ?? {};
	const required = new Set(schema.required ?? []);

	const fields = Object.entries(props)
		.filter(([k]) => k !== "action" && k !== "request_id") // envelope fields excluded
		.map(([k, prop]) => {
			let zodExpr = propToZod(prop);
			if (!required.has(k)) zodExpr += ".optional()";
			return `    ${k}: ${zodExpr}`;
		});

	return `z.object({\n${fields.join(",\n")}\n})`;
}

// Constraint schemas: named scalar schemas (not objects/arrays) reused inside
// payloads. Detected generically from the contract — no hardcoded list.
const constraintSchemas = Object.entries(schemas).filter(
	([, schema]) => schema.type !== "object" && schema.type !== "array"
);

// Payload schemas to generate (all schemas ending in "Payload")
const payloadSchemas = Object.entries(schemas).filter(([name]) => name.endsWith("Payload"));

// ---------------------------------------------------------------------------
// Build output
// ---------------------------------------------------------------------------
const { client: clientActions, server: serverActions } = collectActions();
const xEnums = data?.components?.["x-enums"] ?? {};

const lines = [
	"// AUTO-GENERATED — do not edit manually.",
	"// Source: contract/asyncapi.yaml",
	"// Regenerate: npm run generate:contract",
	"import { z } from 'zod';",
	"",
	"// ---------------------------------------------------------------------------",
	"// Validation constants (single source of truth — derived from constraint schemas)",
	"// ---------------------------------------------------------------------------"
];

// Emit constants from every x-constants annotation. Seed schemas-first, then
// messages, then the rest of the document, for stable human-friendly order.
const constants = {};
collectConstants(schemas, constants);
collectConstants(messages, constants);
collectConstants(data, constants);
for (const [name, value] of Object.entries(constants)) {
	lines.push(`export const ${name} = ${value} as const;`);
}
lines.push("");

// Constraint Zod schemas
lines.push("// ---------------------------------------------------------------------------");
lines.push("// Constraint schemas (reused inside payload schemas)");
lines.push("// ---------------------------------------------------------------------------");
for (const [name, schema] of constraintSchemas) {
	lines.push(`export const ${name}Schema = ${propToZod(schema)};`);
	lines.push(`export type ${name} = z.infer<typeof ${name}Schema>;`);
}
lines.push("");

// Action constants
lines.push("// ---------------------------------------------------------------------------");
lines.push("// Action string constants");
lines.push("// ---------------------------------------------------------------------------");
lines.push("export const ClientAction = {");
for (const [key, val] of Object.entries(clientActions)) {
	lines.push(`    ${key}: '${val}',`);
}
lines.push("} as const;");
lines.push("export type ClientActionType = (typeof ClientAction)[keyof typeof ClientAction];");
lines.push("");
lines.push("export const ServerAction = {");
for (const [key, val] of Object.entries(serverActions)) {
	lines.push(`    ${key}: '${val}',`);
}
lines.push("} as const;");
lines.push("export type ServerActionType = (typeof ServerAction)[keyof typeof ServerAction];");
lines.push("");

// x-enums
lines.push("// ---------------------------------------------------------------------------");
lines.push("// Shared enum constants (Type, Value, Action)");
lines.push("// ---------------------------------------------------------------------------");
for (const [enumName, enumDef] of Object.entries(xEnums)) {
	const entries = Object.entries(enumDef.values ?? {});
	lines.push(`export const ${enumName} = {`);
	for (const [key, val] of entries) {
		const rendered = typeof val === "number" ? val : `'${val}'`;
		lines.push(`    ${key}: ${rendered},`);
	}
	lines.push("} as const;");
	lines.push(`export type ${enumName} = (typeof ${enumName})[keyof typeof ${enumName}];`);
	lines.push("");
}

// Payload schemas
lines.push("// ---------------------------------------------------------------------------");
lines.push("// Client → Server payload schemas");
lines.push("// ---------------------------------------------------------------------------");
for (const [name, schema] of payloadSchemas) {
	const zodExpr = schemaToZod(name, schema);
	lines.push(`export const ${name}Schema = ${zodExpr};`);
	lines.push(`export type ${name} = z.infer<typeof ${name}Schema>;`);
	lines.push("");
}

// Per-action outgoing message schemas (payload + action literal + optional request_id)
// Used by ws.svelte.ts to validate outgoing frames at runtime.
lines.push("// ---------------------------------------------------------------------------");
lines.push("// Outgoing message schemas (payload + action discriminator)");
lines.push("// ---------------------------------------------------------------------------");
for (const [msgName, msg] of Object.entries(messages)) {
	const actionStr = msg.name;
	if (!actionStr) continue;

	// Only generate for client → server messages that reference a payload schema
	const payloadRef = msg.payload?.$ref;
	if (!payloadRef) continue;
	const payloadSchemaName = payloadRef.split("/").pop();
	if (!payloadSchemaName?.endsWith("Payload")) continue;

	lines.push(`export const ${msgName}MessageSchema = ${payloadSchemaName}Schema.extend({`);
	lines.push(`    action: z.literal('${actionStr}'),`);
	lines.push(`    request_id: z.string().optional(),`);
	lines.push("});");
	lines.push(`export type ${msgName}Message = z.infer<typeof ${msgName}MessageSchema>;`);
	lines.push("");
}

// Outgoing schemas lookup — used by ws.svelte.ts to validate outgoing frames at runtime.
// Keyed by action string; value is the corresponding MessageSchema (payload + action literal).
lines.push("// ---------------------------------------------------------------------------");
lines.push("// outgoingSchemas — action string → outgoing message Zod schema");
lines.push("// ---------------------------------------------------------------------------");
lines.push("export const outgoingSchemas: Partial<Record<string, z.ZodTypeAny>> = {");
for (const [msgName, msg] of Object.entries(messages)) {
	const actionStr = msg.name;
	if (!actionStr) continue;
	const payloadRef = msg.payload?.$ref;
	if (!payloadRef) continue;
	const payloadSchemaName = payloadRef.split("/").pop();
	if (!payloadSchemaName?.endsWith("Payload")) continue;
	lines.push(`    '${actionStr}': ${msgName}MessageSchema,`);
}
lines.push("};");
lines.push("");

// ClientPayloads map — used for TypeScript overload resolution in ws.svelte.ts
lines.push("// ---------------------------------------------------------------------------");
lines.push("// ClientPayloads — maps each action string to its payload type");
lines.push("// ---------------------------------------------------------------------------");
lines.push("export interface ClientPayloads {");
for (const [msgName, msg] of Object.entries(messages)) {
	const actionStr = msg.name;
	if (!actionStr) continue;
	const payloadRef = msg.payload?.$ref;
	if (!payloadRef) continue;
	const payloadSchemaName = payloadRef.split("/").pop();
	if (!payloadSchemaName?.endsWith("Payload")) continue;
	lines.push(`    '${actionStr}': ${payloadSchemaName};`);
}
// No-payload client actions get Record<string, never>
// Only include actions that appear in the publish channel (client → server), deduplicated.
const clientActionValues = new Set(Object.values(clientActions));
const emittedPayloadActions = new Set(
	Object.entries(messages)
		.filter(([, msg]) => msg.name && msg.payload?.$ref)
		.map(([, msg]) => msg.name)
);
const seenNoPayload = new Set();
const noPayloadMessages = Object.entries(messages).filter(
	([, msg]) =>
		msg.name &&
		!msg.payload?.$ref &&
		clientActionValues.has(msg.name) &&
		!emittedPayloadActions.has(msg.name)
);
for (const [, msg] of noPayloadMessages) {
	if (!msg.name || seenNoPayload.has(msg.name)) continue;
	seenNoPayload.add(msg.name);
	lines.push(`    '${msg.name}': Record<string, never>;`);
}
lines.push("}");
lines.push("");

mkdirSync(dirname(OUTPUT), { recursive: true });
writeFileSync(OUTPUT, lines.join("\n"));
console.log(`[generate-schemas] Written ${OUTPUT}`);
