<script lang="ts">
    import { on, emit } from "./ws";
    import { onDestroy } from "svelte";

    let {
        initialCount,
        initialLastClicker,
    }: { initialCount: number; initialLastClicker: string } = $props();

    let count = $state(initialCount);
    let lastClicker = $state(initialLastClicker);

    // on() returns its own cleanup — pass directly to onDestroy
    const cleanup = on("sync_count", (data) => {
        count = data.count as number;
        lastClicker = data.last_clicker as string;
    });

    onDestroy(cleanup);
</script>

<button class="counter" onclick={() => emit("click")}>
    Count: {count}
</button>
<p>Ultimo click: {lastClicker}</p>
