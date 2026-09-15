<script lang="ts">
	import { meshInstances } from "./drawPileMockState";
	let { children, ...rest } = $props();
	$effect.pre(() => {
		meshInstances.push(rest);
	});
</script>

{#if children}
	{@render children()}
{/if}
