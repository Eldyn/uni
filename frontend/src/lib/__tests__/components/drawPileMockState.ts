export interface MockMeshProps {
	position?: [number, number, number];
	"rotation.x"?: number;
	scale?: number;
	onclick?: (e: unknown) => void;
	visible?: boolean;
	[key: string]: unknown;
}

export const meshInstances: MockMeshProps[] = [];
export const cardMeshInstances: Record<string, unknown>[] = [];

export function resetMockState(): void {
	meshInstances.length = 0;
	cardMeshInstances.length = 0;
}
