<script lang="ts">
	let { children, "data-testid": testId, opacity } = $props();
</script>

<div data-testid={testId} data-opacity={opacity}>
	{#if children}
		{@render children()}
	{/if}
</div>
