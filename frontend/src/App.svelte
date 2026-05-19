<script lang="ts">
    import { connect, emitAndWait, socketState } from "./lib/socket.svelte";
    import Counter from "./lib/Counter.svelte";

    import svelteLogo from "./assets/svelte.svg";
    import viteLogo from "./assets/vite.svg";
    import heroImg from "./assets/hero.png";

    let screen: "auth" | "index" | "game" | "buttonTest" = $state("auth");
    let data = { clicks: 0, lastClicker: "Nessuno" };
    let username: string | "login" = $state("login");

    // INFO: Stato del colore in game
    let gameColor = "green";

    async function logout() {
        const response = await fetch(`${window.location.origin}/auth/logout`, {
            method: "POST",
            credentials: "include",
        });

        if (response.ok) {
            screen = "auth";
            username = "Login";
        }
    }

    // INFO: Auth-me request
    (async () => {
        try {
            const response = await fetch(`${window.location.origin}/auth/me`, {
                method: "GET",
                credentials: "include",
                headers: {
                    "Content-Type": "application/json",
                },
            });

            if (response.ok) {
                const data = await response.json();
                screen = "index";
                username = data.username;

                console.log("my name is:", data.username);
            } else {
                logout();
            }
        } catch (err) {
            console.log("Token expired");
        }
    })();

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

            if (response.ok) {
                alert("Registrazione completata! Ora effettua il login.");
            } else {
                console.error("Errore nella registrazione");
            }
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

            if (response.ok) {
                screen = "index";
                username = (await response.json()).username;
            } else {
                alert("Credenziali errate");
            }
        } catch (error) {
            console.error("Fetch error:", error);
        }
    }

    async function handleSubmit(event: SubmitEvent) {
        const formData = new FormData(event.target as HTMLFormElement);
        try {
            const response = await fetch(`${window.location.origin}/room`, {
                method: "POST",
                credentials: "include",
                headers: { "Content-Type": "application/json" },
                body: JSON.stringify({ topic: formData.get("topic") }),
            });

            if (response.status === 422) {
                throw new Error(
                    "Input used illegal characters or was too long",
                );
            }
            if (response.status === 401) {
                alert("fai il login prima");
            }

            if (response.ok) {
                const result = await response.json();
                await connect();

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

                    data = {
                        clicks: queriedData.clicks as number,
                        lastClicker: queriedData.lastClicker as string,
                    };

                    screen = "buttonTest";
                }
            }
        } catch (error) {
            console.error("Fetch error:", error);
        }
    }
</script>

<div>
    <section id="center">
        <div class="hero">
            <button class="counter" on:click={logout}>{username}</button>
            <img src={heroImg} class="base" width="170" height="179" alt="" />
            <img src={svelteLogo} class="framework" alt="Svelte logo" />
            <img src={viteLogo} class="vite" alt="Vite logo" />
            <button
                class="counter"
                on:click={() => {
                    screen = "index";
                }}>vai all'index</button
            >
        </div>
    </section>

    <div class="ticks"></div>

    {#if screen === "auth"}
        <section id="next-steps">
            <div id="docs">
                <h1>Form per Registrarsi</h1>
                <form on:submit|preventDefault={handleRegister}>
                    <label>
                        username:
                        <input class="counter" name="username" required />
                    </label>
                    <br />
                    <label>
                        email:
                        <input
                            class="counter"
                            name="email"
                            type="email"
                            required
                        />
                    </label>
                    <br />
                    <label>
                        password:
                        <input
                            class="counter"
                            name="password"
                            type="password"
                            required
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
                            name="email"
                            type="email"
                            required
                        />
                    </label>
                    <br />
                    <label>
                        password:
                        <input
                            class="counter"
                            name="password"
                            type="password"
                            required
                        />
                    </label>
                    <br />
                    <button class="counter" type="submit">Login!</button>
                </form>
            </div>
        </section>
        <div class="ticks"></div>
        <section id="spacer"></section>
        <div class="ticks"></div>
    {:else if screen === "index"}
        <section id="next-steps">
            <div id="docs">
                <h1>Benvenuto!</h1>
                <h2>Inserisci il nome di una stanza per iniziare a giocare.</h2>
            </div>
            <div>
                <h1>Form per Accedere</h1>
                <form on:submit|preventDefault={handleSubmit}>
                    <label>
                        Nome Stanza:
                        <input
                            class="counter"
                            name="topic"
                            id="topic"
                            required
                        />
                    </label>
                    <button class="counter" type="submit">
                        Accedi alla Stanza!
                    </button>
                </form>
                <button
                    class="counter"
                    on:click={() => {
                        screen = "game";
                    }}
                >
                    Game UI
                </button>
            </div>
        </section>
        <div class="ticks"></div>
        <section id="spacer"></section>
        <div class="ticks"></div>
    {:else if screen === "buttonTest"}
        <section id="next-steps">
            <div id="docs">
                <h1>{socketState.room}</h1>
                <h2>E' la stanza corrente.</h2>
            </div>
            <div>
                <h1>Clicca!</h1>
                <h2>Aumenterà a tutti 😉</h2>
                <Counter
                    initialCount={data.clicks}
                    initialLastClicker={data.lastClicker}
                />
            </div>
        </section>
        <div class="ticks"></div>
        <section id="spacer"></section>
        <ul>
            <li>ciao</li>
            <li>sono</li>
            <li>Alessandro</li>
            <li>Borghese</li>
            <li>diesci</li>
        </ul>
    {:else if screen === "game"}
        <button
            class="counter"
            on:click={() => {
                screen = "index";
            }}
        >
            Back
        </button>
        <section id="next-steps"></section>
        <div class="game-field perspective {gameColor}">
            <div id="player">
                (You)
                <div class="player_hand">
                    {#each Array(2) as _, r}
                        <div class="card red" data-key={r}>
                            <div class="bckg"></div>
                        </div>
                    {/each}

                    {#each Array(2) as _, b}
                        <div class="card blue" data-key={b}>
                            <div class="bckg"></div>
                        </div>
                    {/each}

                    <div class="card yellow"><div class="bckg"></div></div>
                    <div class="card green"><div class="bckg"></div></div>
                    <div class="card black"><div class="bckg"></div></div>
                </div>
            </div>

            <div id="player_left">
                Left Player
                <div class="player_hand">
                    {#each Array(7) as _, n}
                        <div class="card turned" data-index={n}>
                            <div class="bckg"></div>
                        </div>
                    {/each}
                </div>
            </div>

            <div id="player_top">
                Top Player
                <div class="player_hand">
                    {#each Array(7) as _, n}
                        <div class="card turned" data-index={n}>
                            <div class="bckg"></div>
                        </div>
                    {/each}
                </div>
            </div>

            <div id="player_right">
                Right Player
                <div class="player_hand">
                    {#each Array(7) as _, n}
                        <div class="card turned" data-index={n}>
                            <div class="bckg"></div>
                        </div>
                    {/each}
                </div>
            </div>

            <div id="piles_area">
                <div id="draw_pile">
                    <div class="card turned top-card">
                        <div class="bckg"></div>
                    </div>
                    <div class="card turned pile"><div class="bckg"></div></div>
                </div>
                <div id="discard_pile">
                    <div class="card top-card {gameColor}">
                        <div class="bckg"></div>
                    </div>
                    <div class="card pile"><div class="bckg"></div></div>
                </div>
            </div>
        </div>
        <div class="ticks"></div>
        <section id="spacer"></section>
        <div class="ticks"></div>
    {/if}
</div>
