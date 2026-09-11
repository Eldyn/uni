// fake-lobbies.mjs - driven by fake-lobbies.sh, not meant to be run directly.
//
// Each lobby gets a random name, random max_players, a random human headcount
// (1-10, capped at max_players), random leftover bot_count, and a random
// subset of the server's rule catalog as active_mods. Rule ids aren't
// hardcoded here - they're fetched once via metadata_request, since the
// server is the source of truth for what rules exist.
//
// NOTE: the server caps connections at MAX_CONN_PER_IP (default 10) and
// guest logins at RATE_AUTH_BURST (default 10, refilling ~0.5/s). Since this
// script runs every fake player from one IP, asking for many lobbies *and*
// many humans per lobby will start failing past that ceiling - bump
// MAX_CONN_PER_IP/RATE_AUTH_BURST in your local .env if you need more.
import { createRequire } from "node:module";
// NODE_PATH only resolves for CommonJS require(), not ESM import, so pull
// the frontend's `ws` dependency in through that door.
const WebSocket = createRequire(import.meta.url)("ws");

const [, , countArg, baseUrlArg] = process.argv;
const COUNT = Number(countArg) || 8;
const BASE_URL = baseUrlArg || "https://localhost:9999";
const WS_URL = BASE_URL.replace(/^http/, "ws");
const TLS_INSECURE = process.env.NODE_TLS_REJECT_UNAUTHORIZED === "0";

const MAX_LOBBY_MEMBERS = 16; // mirrors contract::kMaxLobbyMembers
const MAX_HUMANS_PER_LOBBY = 1;

const WORDS = [
    "hot", "mega", "chaos", "clutch", "sweaty", "tryhard", "cracked", "goated",
    "unhinged", "feral", "spicy", "cursed", "blessed", "stacked", "loaded",
    "final", "reverse", "combo", "combos", "lobby", "tavern", "arena",
    "gauntlet", "stack", "deck", "hand", "draw", "cards", "100 cards", "big",
    "no mercy", "we hittin", "insane", "legendary", "tilted", "clean",
    "dirty", "quick", "speedrun", "grind", "wombo", "night owls", "degens",
];

function sleep(ms) {
    return new Promise((resolve) => setTimeout(resolve, ms));
}

function randInt(min, max) {
    return min + Math.floor(Math.random() * (max - min + 1));
}

function pick(arr, n) {
    const copy = [...arr];
    for (let i = copy.length - 1; i > 0; i--) {
        const j = Math.floor(Math.random() * (i + 1));
        [copy[i], copy[j]] = [copy[j], copy[i]];
    }
    return copy.slice(0, n);
}

function randomName() {
    const [a, b] = pick(WORDS, 2);
    const raw = `${a} ${b}`;
    return raw.charAt(0).toUpperCase() + raw.slice(1);
}

async function guestLogin() {
    const res = await fetch(`${BASE_URL}/auth/guest`, { method: "POST" });
    if (!res.ok) throw new Error(`guest login failed: ${res.status}`);
    const cookies = res.headers.getSetCookie?.() ?? [res.headers.get("set-cookie")];
    const wsTokenCookie = cookies.find((c) => c?.startsWith("ws_token="));
    if (!wsTokenCookie) throw new Error("no ws_token cookie in guest response");
    const { username } = await res.json();
    return { cookie: wsTokenCookie.split(";")[0], username };
}

function connect(cookie) {
    return new WebSocket(WS_URL, {
        headers: { Cookie: cookie },
        rejectUnauthorized: !TLS_INSECURE,
    });
}

function waitForAction(ws, action, requestId) {
    return new Promise((resolve, reject) => {
        ws.on("message", function handler(raw) {
            const msg = JSON.parse(raw.toString());
            if (msg.request_id !== undefined && msg.request_id !== requestId) return;
            if (msg.action === action) {
                ws.off("message", handler);
                resolve(msg);
            } else if (msg.action === "error") {
                ws.off("message", handler);
                reject(new Error(`server error: ${JSON.stringify(msg)}`));
            }
        });
    });
}

async function fetchRuleIds() {
    const { cookie } = await guestLogin();
    const ws = connect(cookie);
    await new Promise((resolve, reject) => {
        ws.on("open", resolve);
        ws.on("error", reject);
    });
    ws.send(JSON.stringify({ action: "metadata_request", request_id: "rules" }));
    const msg = await waitForAction(ws, "metadata", "rules");
    ws.close();
    return (msg.available_rules ?? []).map((r) => r.id);
}

async function joinAsExtraHuman(code) {
    const { cookie, username } = await guestLogin();
    const ws = connect(cookie);
    await new Promise((resolve, reject) => {
        ws.on("open", resolve);
        ws.on("error", reject);
    });
    ws.send(JSON.stringify({ action: "lobby_join", request_id: "join", code }));
    await waitForAction(ws, "lobby_joined", "join");
    return { ws, username };
}

async function createFakeLobby(ruleIds) {
    const { cookie, username } = await guestLogin();
    const ws = connect(cookie);
    await new Promise((resolve, reject) => {
        ws.on("open", resolve);
        ws.on("error", reject);
    });

    const name = randomName();
    ws.send(JSON.stringify({
        action: "lobby_create",
        request_id: "create",
        is_public: true,
        name,
    }));
    const joined = await waitForAction(ws, "lobby_joined", "create");
    const code = joined.lobby.invite_code;

    const maxPlayers = randInt(2, MAX_LOBBY_MEMBERS);
    const humans = randInt(1, Math.min(MAX_HUMANS_PER_LOBBY, maxPlayers));
    const botCount = randInt(0, maxPlayers);
    const mods = pick(ruleIds, randInt(0, ruleIds.length));

    ws.send(JSON.stringify({
        action: "lobby_update_settings",
        request_id: "settings",
        max_players: maxPlayers,
        bot_count: botCount,
        active_mods: mods,
        allow_bot_takeover: Math.random() < 0.5,
    }));
    await waitForAction(ws, "success", "settings");

    const sockets = [ws];
    const names = [username];

    for (let j = 1; j < humans; j++) {
        await sleep(150); // stay under the auth-endpoint burst limit
        try {
            const { ws: joinerWs, username: joinerName } = await joinAsExtraHuman(code);
            sockets.push(joinerWs);
            names.push(joinerName);
        } catch (err) {
            console.error(`  [fail] extra human ${j} for "${name}":`, err.message);
        }
    }

    console.log(`[ok] "${name}" code=${code} humans=${names.length}/${humans} ` +
        `max=${maxPlayers} bots=${botCount} mods=[${mods.join(", ")}]`);

    return sockets;
}

const ruleIds = await fetchRuleIds();
console.log(`Rule catalog: [${ruleIds.join(", ")}]\n`);

const sockets = [];

for (let i = 0; i < COUNT; i++) {
    try {
        sockets.push(...await createFakeLobby(ruleIds));
    } catch (err) {
        console.error(`[fail] lobby ${i}:`, err.message);
    }
    await sleep(150); // stay under the auth-endpoint burst limit
}

console.log(`\n${sockets.length} connections live across ${COUNT} attempted lobbies ` +
    `against ${BASE_URL}. Ctrl+C to tear down.`);

process.on("SIGINT", () => {
    console.log("\nClosing connections...");
    for (const ws of sockets) ws.close();
    process.exit(0);
});
