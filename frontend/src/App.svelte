<script lang="ts">
    import { connect, emitAndWait, socketState } from "./lib/socket.svelte";
    import Counter from "./lib/Counter.svelte";

    import svelteLogo from "./assets/svelte.svg";
    import viteLogo from "./assets/vite.svg";
    import heroImg from "./assets/hero.png";

    let currentScreen: "index" | "buttonTest" = "index";
    let syncedData: { clicks: number; lastClicker: string };

    async function handleSubmit(event: SubmitEvent) {
        const formData = new FormData(event.target as HTMLFormElement);

        try {
            const response = await fetch(
                `${window.location.protocol}//${window.location.host}/room`,
                {
                    method: "POST",
                    headers: { "Content-Type": "application/json" },
                    body: JSON.stringify({ topic: formData.get("topic") }),
                },
            );

            if (response.status === 422)
                throw new Error(
                    "Input used illegal characters (or was too long)",
                );

            const result = await response.json();

            if (result.status === "OK") {
                await connect("TODO_JWT_token");
                const myData = await emitAndWait(
                    "join",
                    { topic: result.topic },
                    "sync_data",
                );

                if (myData.room) {
                    const queriedData = await emitAndWait(
                        "query",
                        {},
                        "queried",
                    );

                    syncedData = {
                        clicks: queriedData.clicks as number,
                        lastClicker: queriedData.lastClicker as string,
                    };

                    currentScreen = "buttonTest";
                }
            }
        } catch (error) {
            console.error("Fetch error:", error);
        }
    }

    async function handleRegister(event: SubmitEvent) {
        const formData = new FormData(event.target as HTMLFormElement);

        try {
            const response = await fetch(
                `${window.location.origin}/auth/register`,
                {
                    method: "POST",
                    headers: { "Content-Type": "application/json" },
                    body: JSON.stringify({
                        username: formData.get("username"),
                        email: formData.get("email"),
                        password: formData.get("password"),
                    }),
                },
            );
        } catch (error) {
            console.error("Fetch error:", error);
        }
    }

    async function handleLogin(event: SubmitEvent) {
        const formData = new FormData(event.target as HTMLFormElement);

        try {
            const response = await fetch(
                `${window.location.origin}/auth/login`,
                {
                    method: "POST",
                    headers: { "Content-Type": "application/json" },
                    body: JSON.stringify({
                        email: formData.get("email"),
                        password: formData.get("password"),
                    }),
                },
            );
        } catch (error) {
            console.error("Fetch error:", error);
        }
    }
</script>

<div>
    <section id="center">
        <div class="hero">
            <img src={heroImg} class="base" width="170" height="179" alt="" />
            <img src={svelteLogo} class="framework" alt="Svelte logo" />
            <img src={viteLogo} class="vite" alt="Vite logo" />
        </div>
        <div>
            <h1>Test Uni-Uno</h1>
            <p>
                Edit <code>src/App.svelte</code> and save to test
                <code>HMR</code>
            </p>
            <br />
        </div>
    </section>

    <div class="ticks"></div>

    {#if currentScreen === "index"}
        <section id="next-steps">
            <div id="docs">
                <h1>E' un test</h1>
                <h2>non si vede?</h2>
            </div>
            <div>
                <h1>Form per Accedere</h1>
                <form on:submit|preventDefault={handleSubmit}>
                    <label>
                        Nome Stanza:
                        <input class="counter" name="topic" id="topic" />
                    </label>
                    <button class="counter" type="submit">Accedi!</button>
                </form>
            </div>
        </section>
        <div class="ticks"></div>
    {:else if currentScreen === "buttonTest"}
        <section id="next-steps">
            <div id="docs">
                <h1>{socketState.room}</h1>
                <h2>E' la stanza corrente.</h2>
            </div>
            <div>
                <h1>Clicca!</h1>
                <h2>Aumenterà a tutti 😉</h2>
                <Counter
                    initialCount={syncedData.clicks}
                    initialLastClicker={syncedData.lastClicker}
                />
            </div>
        </section>
        <div class="ticks"></div>
    {/if}

    <div class="ticks"></div>
    <section id="spacer"></section>
    <div class="ticks"></div>

    <section id="next-steps">
        <div id="docs">
            <h1>Form per Registrarsi</h1>
            <form on:submit|preventDefault={handleRegister}>
                <label>
                    username:
                    <input class="counter" name="topic" id="topic" />
                </label>
                <br />
                <label>
                    email:
                    <input
                        class="counter"
                        name="topic"
                        id="topic"
                        type="email"
                    />
                </label>
                <br />
                <label>
                    password:
                    <input
                        class="counter"
                        name="topic"
                        id="topic"
                        type="password"
                    />
                </label>
                <br />
                <button class="counter" type="submit">Registrati!</button>
            </form>
        </div>

        <div>
            <h1>Form per Login</h1>
            <form on:submit|preventDefault={handleLogin}>
                <label>
                    email:
                    <input
                        class="counter"
                        name="topic"
                        id="topic"
                        type="email"
                    />
                </label>
                <br />
                <label>
                    password:
                    <input
                        class="counter"
                        name="topic"
                        id="topic"
                        type="password"
                    />
                </label>
                <br />
                <button class="counter" type="submit">Login!</button>
            </form>
        </div>
    </section>

    <div class="ticks"></div>
    <section id="spacer"></section>
</div>
