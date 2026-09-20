/**
 * @file spectator.svelte.ts
 * @brief Owns which player's POV a spectator is currently viewing, plus the
 * full-screen darken/fade used to cover a POV switch. Kept separate from
 * storeGame so both the topbar avatar-row click () and the in-scene
 * opponent click () can drive the same piece of state without one UI
 * surface importing the other.
 *
 * Switching POV used to run through storeTableSpin's animated table rotation.
 * That is gone: instead the screen darkens to black, the POV is swapped at the
 * darkest point (under cover, so the board's shape change is never seen), then
 * the screen fades back to reveal the new player's view. The timeline lives
 * here, not in a component, so any surface that calls setViewedUsername gets
 * the exact same transition without coordinating with a render layer.
 */
import { gsap } from "gsap";
import { storeAnimation } from "./animation.svelte";
import { storeGame } from "./game.svelte";
import { ClientAction, ws } from "./ws.svelte";

/** Seconds to reach full black, and to fade back out. Short enough to feel
 *  like a blink, long enough to read as a deliberate cut rather than a glitch. */
const DARKEN_SECONDS = 0.26;
const LIGHTEN_SECONDS = 0.3;

class StoreSpectator {
	/** Username of the player whose POV is currently being viewed, or null if
	 *  no explicit choice has been made yet (defaults to the local view). */
	viewedUsername = $state<string | null>(null);

	/** 0 → 1 black overlay opacity for the POV-switch fade. Rendered by
	 *  SpectatorFade.svelte; 0 means the overlay is not mounted at all. */
	fadeOpacity = $state(0);

	/** True from the first frame of a fade until the last — used to block
	 *  input while the screen is covered so a second click can't queue a
	 *  competing switch. */
	transitioning = $state(false);

	/** True while the local client is spectating (mirrors storeGame.isSpectator,
	 *  exposed here too so consumers of this store don't also need storeGame). */
	isSpectating = $derived(storeGame.isSpectator);

	#fadeTl: gsap.core.Timeline | null = null;

	/** Tells the server which player this spectator is watching, so the
	 *  per-player spectator counts in the match state stay accurate. An empty
	 *  username means "no explicit choice" (the server attributes the
	 *  spectator to whoever's turn it is). Fire-and-forget and a no-op when the
	 *  socket isn't open. */
	#notifyServer(): void {
		ws.emit(ClientAction.SpectatorView, { viewed_username: this.viewedUsername ?? "" });
	}

	setViewedUsername(username: string): void {
		if (!this.isSpectating) return;
		if (username === this.viewedUsername && !this.transitioning) return;

		// A re-click during a fade restarts cleanly rather than stacking two
		// timelines that both drive fadeOpacity.
		this.#fadeTl?.kill();
		this.#fadeTl = null;

		// Animation off (or reduced motion) is an accessibility contract: swap
		// instantly with no flash rather than playing a faster fade.
		if (!storeAnimation.enabled) {
			this.viewedUsername = username;
			this.fadeOpacity = 0;
			this.transitioning = false;
			this.#notifyServer();
			return;
		}

		const speed = Math.max(0.1, storeAnimation.speedMultiplier);
		const proxy = { t: 0 };
		this.transitioning = true;
		this.fadeOpacity = 0;

		const tl = gsap.timeline({
			onComplete: () => {
				this.transitioning = false;
				this.fadeOpacity = 0;
				if (this.#fadeTl === tl) this.#fadeTl = null;
			}
		});
		tl.to(proxy, {
			t: 1,
			duration: DARKEN_SECONDS / speed,
			ease: "power2.in",
			onUpdate: () => (this.fadeOpacity = proxy.t)
		});
		// The swap happens exactly at full black, so the board's layout change
		// is never visible — only the new POV is, as the screen lifts.
		tl.call(() => {
			this.viewedUsername = username;
			this.#notifyServer();
		});
		tl.to(proxy, {
			t: 0,
			duration: LIGHTEN_SECONDS / speed,
			ease: "power2.out",
			onUpdate: () => (this.fadeOpacity = proxy.t)
		});
		this.#fadeTl = tl;
	}

	reset(): void {
		this.#fadeTl?.kill();
		this.#fadeTl = null;
		this.viewedUsername = null;
		this.fadeOpacity = 0;
		this.transitioning = false;
	}
}

export const storeSpectator = new StoreSpectator();
