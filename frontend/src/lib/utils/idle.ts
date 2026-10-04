/**
 * @file idle.ts
 * @brief Resolves once the page has finished loading and the browser is idle,
 * so decorative work can queue behind everything the user is waiting on.
 */

const IDLE_FALLBACK_DELAY_MS = 200;

export function whenIdle(timeoutMs = 4000): Promise<void> {
	return new Promise((resolve) => {
		const waitForIdle = () => {
			if ("requestIdleCallback" in window) {
				window.requestIdleCallback(() => resolve(), { timeout: timeoutMs });
			} else {
				setTimeout(resolve, IDLE_FALLBACK_DELAY_MS);
			}
		};

		if (document.readyState === "complete") {
			waitForIdle();
		} else {
			window.addEventListener("load", waitForIdle, { once: true });
		}
	});
}
