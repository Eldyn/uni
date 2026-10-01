<script lang="ts">
	let { children, "data-testid": testId, opacity, "scale.x": scaleX } = $props();
</script>

<div data-testid={testId} data-opacity={opacity} data-scale-x={scaleX}>
	{#if children}
		{@render children()}
	{/if}
</div>
