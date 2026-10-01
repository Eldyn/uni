<script lang="ts">
	let { position }: { position?: [number, number, number] } = $props();
</script>

<div data-testid="scene-camera" data-position={JSON.stringify(position ?? null)}></div>
