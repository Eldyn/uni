import type { FlightHandle } from "./cardRegistry.svelte";

/**
 * Whether a registry entry may be inspected (right-click / long-press detail
 * popover) by the local player.
 *
 * Face-down entries — opponent ring cards and withheld spectator hands — are
 * registered with `turned: true`, a synthetic `ring:<player>:<slot>` id, and
 * dummy `{ type: "wild", value: "0" }` meta. Their meta must never be read as
 * a real card, so inspection is limited to entries that are face-up, not
 * mid-flight, and not owned by another player.
 */
export function isInspectable(handle: FlightHandle, inTransit = false): boolean {
	return !inTransit && !handle.pose.turned && !handle.id.startsWith("ring:");
}
