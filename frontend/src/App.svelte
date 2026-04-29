<script lang="ts">
  import svelteLogo from "./assets/svelte.svg";
  import viteLogo from "./assets/vite.svg";
  import heroImg from "./assets/hero.png";
  import Counter from "./lib/Counter.svelte";

  async function handleSubmit(event: SubmitEvent) {
    const formData = new FormData(event.target as HTMLFormElement);

    console.log(JSON.stringify({ topic: formData.get("topic") }));

    try {
      const response = await fetch("https://localhost:9999/create-topic", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ topic: formData.get("topic") }),
      });
      const result = await response.json();
      console.log(result);

      if (result.status === "OK") {
        const host = window.location.host;
        const protocol = window.location.protocol === "https:" ? "wss:" : "ws:";
        const socketUrl = `${protocol}//${host}/ws`;
        const socket = new WebSocket(socketUrl);

        socket.onopen = () => {
          socket.send(JSON.stringify({ action: "join", topic: result.topic }));
        };
      }
    } catch (error) {
      console.error("Errore durante il fetch:", error);
    }
  }
</script>

<section id="center">
  <div class="hero">
    <img src={heroImg} class="base" width="170" height="179" alt="" />
    <img src={svelteLogo} class="framework" alt="Svelte logo" />
    <img src={viteLogo} class="vite" alt="Vite logo" />
  </div>
  <div>
    <h1>Get started</h1>
    <p>Edit <code>src/App.svelte</code> and save to test <code>HMR</code></p>
  </div>
  <Counter />
</section>

<div class="ticks"></div>

<section id="next-steps">
  <div>
    <h2>E' un test <br /> non si vede?</h2>
  </div>
  <div>
    <h1>Form per il join</h1>
    <form on:submit|preventDefault={handleSubmit}>
      <label>
        Nome Topic:
        <input class="counter" name="topic" id="topic" />
      </label>
      <button class="counter" type="submit">Join</button>
    </form>
  </div>
</section>

<div class="ticks"></div>

<section id="next-steps">
  <div id="docs">
    <svg class="icon" role="presentation" aria-hidden="true">
      <use href="/icons.svg#documentation-icon"></use>
    </svg>
    <h2>Documentation</h2>
    <p>Your questions, answered</p>
    <ul>
      <li>
        <a href="https://vite.dev/" target="_blank" rel="noreferrer">
          <img class="logo" src={viteLogo} alt="" />
          Explore Vite
        </a>
      </li>
      <li>
        <a href="https://svelte.dev/" target="_blank" rel="noreferrer">
          <img class="button-icon" src={svelteLogo} alt="" />
          Learn more
        </a>
      </li>
    </ul>
  </div>
  <div id="social">
    <svg class="icon" role="presentation" aria-hidden="true">
      <use href="/icons.svg#social-icon"></use>
    </svg>
    <h2>Connect with us</h2>
    <p>Join the Vite community</p>
    <ul>
      <li>
        <a
          href="https://github.com/vitejs/vite"
          target="_blank"
          rel="noreferrer"
        >
          <svg class="button-icon" role="presentation" aria-hidden="true">
            <use href="/icons.svg#github-icon"></use>
          </svg>
          GitHub
        </a>
      </li>
      <li>
        <a href="https://chat.vite.dev/" target="_blank" rel="noreferrer">
          <svg class="button-icon" role="presentation" aria-hidden="true">
            <use href="/icons.svg#discord-icon"></use>
          </svg>
          Discord
        </a>
      </li>
      <li>
        <a href="https://x.com/vite_js" target="_blank" rel="noreferrer">
          <svg class="button-icon" role="presentation" aria-hidden="true">
            <use href="/icons.svg#x-icon"></use>
          </svg>
          X.com
        </a>
      </li>
      <li>
        <a
          href="https://bsky.app/profile/vite.dev"
          target="_blank"
          rel="noreferrer"
        >
          <svg class="button-icon" role="presentation" aria-hidden="true">
            <use href="/icons.svg#bluesky-icon"></use>
          </svg>
          Bluesky
        </a>
      </li>
    </ul>
  </div>
</section>

<div class="ticks"></div>
<section id="spacer"></section>
