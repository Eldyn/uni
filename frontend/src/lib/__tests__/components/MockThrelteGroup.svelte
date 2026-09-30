<script lang="ts">
	let {
		children,
		"data-testid": testId,
		"position.x": positionX,
		"position.z": positionZ,
		"rotation.y": rotationY
	} = $props();
</script>

<div data-testid={testId} data-x={positionX} data-z={positionZ} data-rotation-y={rotationY}>
	{#if children}
		{@render children()}
	{/if}
</div>
