import { mount } from "svelte";
import "@hackernoon/pixel-icon-library/fonts/iconfont.css";
import "./app.css";
import App from "./App.svelte";

// The static #seo-splash markup inside #app is real, crawlable content —
// It used to be deleted here
// so the running app wouldn't render on top of it; instead, mount into a
// sibling element and let the app's own logged-out landing decide what to
// show, rather than erasing content a crawler would otherwise index.
const appRoot = document.getElementById("app") ?? document.body;
const mountTarget = document.createElement("div");
mountTarget.id = "svelte-app";
appRoot.after(mountTarget);

const app = mount(App, { target: mountTarget });

// Now that the interactive app has mounted, the static splash has done its
// job (first paint, no-JS fallback, crawlable content for anyone who never
// runs this script). Hiding it — not removing it — keeps it in the document
// for a crawler that renders but doesn't fully settle JS, while taking it
// out of the visible page and the accessibility tree for everyone else.
appRoot.hidden = true;

export default app;
