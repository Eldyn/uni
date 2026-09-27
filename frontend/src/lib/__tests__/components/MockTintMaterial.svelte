<script lang="ts">
	import { meshInstances } from "./drawPileMockState";

	let { children, color, ...rest } = $props();

	$effect(() => {
		meshInstances.push({ ...rest, color });
	});
</script>

{#if children}
	{@render children()}
{/if}
