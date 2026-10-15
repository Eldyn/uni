import { writable } from 'svelte/store';

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
