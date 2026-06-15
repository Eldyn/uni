# 🃏 Uni-Uno — Frontend

Frontend del progetto **Uni-Uno**, realizzato con **Svelte 5** (runes) e **TypeScript**,
buildato con **Vite**. Comunica con il backend C++ tramite un client **WebSocket**
centralizzato (canale fire-and-forget `emit` e canale richiesta/risposta `emitAndWait`),
con riconnessione automatica.

## 🧩 Struttura

- `src/lib/stores/` — store singleton reattivi, uno per dominio: `auth`, `lobby`, `game`,
  `navigation`, `toast`, `stats`, `ws`.
- `src/lib/components/game/` — il tavolo di gioco: `GameBoard`, `PlayerHand`,
  `OpponentHand`, `GamePiles`, `GameCard`, `FlyingCardsOverlay` (animazioni), `card-bus`.
- `src/lib/components/lobby/` — schermate e controlli di lobby e impostazioni.
- `public/` — asset statici (immagini delle carte, audio, font pixel-art).

Il riordino delle carte in mano usa il **drag-and-drop** (`@dnd-kit/svelte`).

## 📦 Dipendenze di sistema

Servono solo **Node.js** e **npm**:

```bash
npm install
```

## 🛠️ Comandi utili

```bash
npm run watch    # rebuild automatico ad ogni modifica (output in ../public), da usare col server
npm run preview  # anteprima locale del build con il dev server di Vite
npm run build    # build di produzione (output in ../public)
npm run docs     # genera la documentazione TSDoc (Markdown) in docs/markdown
npm run pdf      # converte la documentazione TSDoc in frontend-manual.pdf
npm run format   # formatta il codice con Prettier
npm run lint     # verifica la formattazione

../deploy_frontend.sh   # build + copia del sito nella cartella servita dal server
```

> Per eseguire il gioco completo (server HTTPS + frontend) e per generare i certificati
> TLS, fai riferimento al [README principale](../README.md).
