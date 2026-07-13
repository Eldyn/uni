# 🃏 UNI, Frontend

Frontend of the **UNI** project, built with **Svelte 5** (runes) and **TypeScript**,
bundled with **Vite**. It communicates with the C++ backend through a centralized
**WebSocket** client (fire-and-forget `emit` channel and request/response `emitAndWait`
channel), with automatic reconnection.

## 🧩 Structure

- `src/lib/stores/`, reactive singleton stores, one per domain: `auth`, `lobby`, `game`,
  `navigation`, `toast`, `stats`, `ws`.
- `src/lib/components/game/`, the game table: `GameBoard`, `PlayerHand`,
  `OpponentHand`, `GamePiles`, `GameCard`, `FlyingCardsOverlay` (animations), `card-bus`.
- `src/lib/components/lobby/`, lobby and settings screens and controls.
- `public/`, static assets (card images, audio, pixel-art fonts).

Reordering the cards in hand uses **drag-and-drop** (`@dnd-kit/svelte`).

## 📦 System dependencies

You only need **Node.js** and **npm**:

```bash
npm install
```

## 🛠️ Useful commands

```bash
npm run watch    # automatic rebuild on every change (output in ../public), to use with the server
npm run preview  # local preview of the build with the Vite dev server
npm run build    # production build (output in ../public)
npm run docs     # generates the TSDoc documentation (Markdown) in docs/markdown
npm run pdf      # converts the TSDoc documentation into frontend-manual.pdf
npm run format   # formats the code with Prettier
npm run lint     # checks the formatting
```

> To run the complete game (HTTPS server + frontend) and to generate the TLS
> certificates, refer to the [main README](../README.md).
