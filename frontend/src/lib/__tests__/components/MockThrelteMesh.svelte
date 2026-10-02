<script lang="ts">
	let {
		children,
		"data-testid": testId,
		opacity,
		"scale.x": scaleX,
		"data-direction": direction
	} = $props();
</script>

<div data-testid={testId} data-opacity={opacity} data-scale-x={scaleX} data-direction={direction}>
	{#if children}
		{@render children()}
	{/if}
</div>
