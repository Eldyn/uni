// Mod graphics SP3 slice 1: the fixed, engine-owned uniform ABI (v1).
// Names are stable and additive-only; version bumps ride the mod `api` gate.

export const UNIFORM_ABI_VERSION = 1;

export const ENGINE_UNIFORM_NAMES = Object.freeze([
	"uTime",
	"uDelta",
	"uVelocity",
	"uSpeed",
	"uProgress",
	"uAge",
	"uLifetime",
	"uOutcomeValue",
	"uOutcomeIndex",
	"uActiveColor",
	"uTargetPosition",
	"uOriginPosition"
] as const);

export const POST_UNIFORM_NAMES = Object.freeze([
	"uResolution",
	"uSceneColor",
	"uIntensity"
] as const);

export const MOD_PARAM_SLOTS = 8;

const MOD_PARAM_DECL = "uniform vec4 uParams[8];\n";

/** GLSL declarations for an engine-fed fragment shader (plus the mod block). */
export const FRAGMENT_UNIFORM_PRELUDE = [
	"uniform float uTime;",
	"uniform float uDelta;",
	"uniform vec3 uVelocity;",
	"uniform float uSpeed;",
	"uniform float uProgress;",
	"uniform float uAge;",
	"uniform float uLifetime;",
	"uniform float uOutcomeValue;",
	"uniform float uOutcomeIndex;",
	"uniform vec3 uActiveColor;",
	"uniform vec3 uTargetPosition;",
	"uniform vec3 uOriginPosition;",
	MOD_PARAM_DECL
].join("\n");

/** GLSL declarations for an engine-fed post shader (plus the mod block). */
export const POST_UNIFORM_PRELUDE = [
	"uniform vec2 uResolution;",
	"uniform sampler2D uSceneColor;",
	"uniform float uIntensity;",
	MOD_PARAM_DECL
].join("\n");

export type Vec2Tuple = [number, number];
export type Vec3Tuple = [number, number, number];
export type Vec4Tuple = [number, number, number, number];

export interface UniformValue<T> {
	value: T;
}

export interface UniformBlock {
	uTime: UniformValue<number>;
	uDelta: UniformValue<number>;
	uVelocity: UniformValue<Vec3Tuple>;
	uSpeed: UniformValue<number>;
	uProgress: UniformValue<number>;
	uAge: UniformValue<number>;
	uLifetime: UniformValue<number>;
	uOutcomeValue: UniformValue<number>;
	uOutcomeIndex: UniformValue<number>;
	uActiveColor: UniformValue<Vec3Tuple>;
	uTargetPosition: UniformValue<Vec3Tuple>;
	uOriginPosition: UniformValue<Vec3Tuple>;
	uResolution: UniformValue<Vec2Tuple>;
	uSceneColor: UniformValue<unknown>;
	uIntensity: UniformValue<number>;
	uParams: UniformValue<Vec4Tuple[]>;
}

export interface UniformFrame {
	uTime?: number;
	uDelta?: number;
	uVelocity?: Vec3Tuple;
	uSpeed?: number;
	uProgress?: number;
	uAge?: number;
	uLifetime?: number;
	uOutcomeValue?: number;
	uOutcomeIndex?: number;
	uActiveColor?: Vec3Tuple;
	uTargetPosition?: Vec3Tuple;
	uOriginPosition?: Vec3Tuple;
}

const ENGINE_FLOAT_NAMES = [
	"uTime",
	"uDelta",
	"uSpeed",
	"uProgress",
	"uAge",
	"uLifetime",
	"uOutcomeValue",
	"uOutcomeIndex"
] as const;

const ENGINE_VEC3_NAMES = [
	"uVelocity",
	"uActiveColor",
	"uTargetPosition",
	"uOriginPosition"
] as const;

export function createUniformBlock(): UniformBlock {
	const slots: Vec4Tuple[] = [];
	for (let i = 0; i < MOD_PARAM_SLOTS; i++) {
		slots.push([0, 0, 0, 0]);
	}
	return {
		uTime: { value: 0 },
		uDelta: { value: 0 },
		uVelocity: { value: [0, 0, 0] },
		uSpeed: { value: 0 },
		uProgress: { value: 0 },
		uAge: { value: 0 },
		uLifetime: { value: 0 },
		uOutcomeValue: { value: 0 },
		uOutcomeIndex: { value: 0 },
		uActiveColor: { value: [0, 0, 0] },
		uTargetPosition: { value: [0, 0, 0] },
		uOriginPosition: { value: [0, 0, 0] },
		uResolution: { value: [0, 0] },
		uSceneColor: { value: null },
		uIntensity: { value: 0 },
		uParams: { value: slots }
	};
}

/** Writes a frame into the block in place; omitted fields keep their value. */
export function updateUniformBlock(block: UniformBlock, frame: UniformFrame): void {
	for (const name of ENGINE_FLOAT_NAMES) {
		const v = frame[name];
		if (v !== undefined) {
			block[name].value = v;
		}
	}
	for (const name of ENGINE_VEC3_NAMES) {
		const v = frame[name];
		if (v !== undefined) {
			const slot = block[name].value;
			slot[0] = v[0];
			slot[1] = v[1];
			slot[2] = v[2];
		}
	}
}

/** Fills the mod block: clamps to 8 vec4, pads zeros, drops non-finite components. */
export function setParams(block: UniformBlock, params: number[][]): void {
	const slots = block.uParams.value;
	for (let i = 0; i < MOD_PARAM_SLOTS; i++) {
		const row = slots[i];
		const src = params[i];
		for (let c = 0; c < 4; c++) {
			const v = src === undefined ? undefined : src[c];
			row[c] = typeof v === "number" && Number.isFinite(v) ? v : 0;
		}
	}
}
