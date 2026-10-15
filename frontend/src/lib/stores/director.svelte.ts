import { writable, get } from 'svelte/store';

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
