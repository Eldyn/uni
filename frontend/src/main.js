import { mount } from "svelte";
import "@hackernoon/pixel-icon-library/fonts/iconfont.css";
import "./app.css";
import App from "./App.svelte";

const target = document.getElementById("app") ?? document.body;

// Remove the static SEO/no-JS fallback markup before mounting so the app does
// not stack on top of it (Svelte's mount appends to the target).
target.replaceChildren();

const app = mount(App, { target });

export default app;
