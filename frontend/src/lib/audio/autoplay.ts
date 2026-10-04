/**
 * @file autoplay.ts
 * @brief Resolves once the browser will let audio start, so music never
 * queues up against a locked AudioContext / autoplay block.
 */

type AutoplayType = "mediaelement" | "audiocontext";

interface NavigatorWithAutoplayPolicy extends Navigator {
	getAutoplayPolicy?: (type: AutoplayType) => "allowed" | "allowed-muted" | "disallowed";
}

const GESTURE_EVENTS = ["pointerdown", "keydown"] as const;

function autoplayAlreadyAllowed(): boolean {
	const { getAutoplayPolicy } = navigator as NavigatorWithAutoplayPolicy;
	if (!getAutoplayPolicy) return false;
	const policy = getAutoplayPolicy.bind(navigator);
	return policy("mediaelement") === "allowed" && policy("audiocontext") === "allowed";
}

export function whenAutoplayAllowed(): Promise<void> {
	if (autoplayAlreadyAllowed()) return Promise.resolve();

	return new Promise((resolve) => {
		const onGesture = () => {
			for (const eventName of GESTURE_EVENTS) {
				document.removeEventListener(eventName, onGesture, true);
			}
			resolve();
		};
		for (const eventName of GESTURE_EVENTS) {
			document.addEventListener(eventName, onGesture, true);
		}
	});
}
