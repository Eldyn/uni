import { writable, get } from "svelte/store";
import { ClientAction, ws } from "./ws.svelte";

interface DirectorState {
	cleanView: boolean;
	hidePlayers: string[];
	xRay: boolean;
}

const base: DirectorState = {
	cleanView: false,
	hidePlayers: [],
	xRay: true
};

export const director = writable<DirectorState>(base);

/**
 * Global class mirrored from cleanView. The chat dock mounts at the app
 * root, outside the game screen, so it cannot be gated by a GameScreen
 * {#if} — an ordinary global stylesheet hides it (and any other chrome)
 * while this class is present. PromptRenderer stays mounted: the director
 * still acts while the frame is bare.
 */
export const CLEAN_VIEW_BODY_CLASS = "director-clean-view";

function syncCleanViewBodyClass(cleanView: boolean): void {
	if (typeof document === "undefined") return;
	document.body.classList.toggle(CLEAN_VIEW_BODY_CLASS, cleanView);
}

if (typeof document !== "undefined") {
	syncCleanViewBodyClass(get(director).cleanView);
	director.subscribe((s) => syncCleanViewBodyClass(s.cleanView));
}

export function setCleanView(value: boolean) {
	director.update((s) => ({ ...s, cleanView: value }));
}

export function togglePlayerHidden(username: string) {
	director.update((s) => ({
		...s,
		hidePlayers: s.hidePlayers.includes(username)
			? s.hidePlayers.filter((u) => u !== username)
			: [...s.hidePlayers, username]
	}));
}

export function setXRay(value: boolean) {
	director.update((s) => ({ ...s, xRay: value }));
}

/**
 * Sends a host-only Creative command over the existing
 * director_command path. No-arg ops (pause | step | force_turn) send
 * `{ op }` with no args key; every other op carries its args verbatim.
 */
export function sendDirectorCommand(op: string, args?: Record<string, unknown>) {
	if (args === undefined) {
		ws.emit(ClientAction.DirectorCommand, { op });
	} else {
		ws.emit(ClientAction.DirectorCommand, { op, args });
	}
}

/**
 * Resolves which avatar strip a seat renders: the snapshot `skin`
 * override wins ("bot" forces the bot strip, "player" forces the player
 * strip), otherwise the existing is_bot / bot-in-username logic applies.
 */
export function resolveSeatIsBot(player: {
	is_bot: boolean;
	username?: string | null;
	skin?: string | null;
}): boolean {
	if (player.skin === "bot") return true;
	if (player.skin === "player") return false;
	return player.is_bot || (player.username?.toLowerCase().includes("bot") ?? false);
}
