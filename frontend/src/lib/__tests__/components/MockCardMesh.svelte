<script lang="ts">
	import { cardMeshInstances } from "./drawPileMockState";
	let props = $props();
	$effect.pre(() => {
		cardMeshInstances.push(props);
	});
</script>
