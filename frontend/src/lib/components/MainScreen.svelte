<script lang="ts">
    import { storeNavigation } from "../stores/navigation.svelte";
</script>

<div class="screen-container">
    
    <header class="logo-container">
        <img 
            src="https://via.placeholder.com/200x80?text=LOGO" 
            alt="Placeholder Logo" 
            class="logo" 
        />
    </header>

    <main class="button-container">
        <button onclick={() => storeNavigation.goto("auth")}>
            Accedi / Registrati
        </button>
    </main>

</div>

<style>
  .screen-container {
    display: flex;
    flex-direction: column;
    min-height: 100vh;
    background: var(--bg);
  }

  .logo-container {
    margin-top: 5%;
    display: flex;
    justify-content: center;
    padding-top: 2rem; 
  }

  .logo {
    border-radius: 8px; 
  }

  .button-container {
    flex: 1;
    display: flex;
    justify-content: center;
    align-items: center;
  }

  button {
    padding: 1rem 2rem;
    font-size: 1.2rem;
    font-weight: bold;
    color: white;
    background-color: #8d42d3; 
    border: none;
    border-radius: 8px;
    cursor: pointer;
    box-shadow: 0 4px 6px rgba(0, 0, 0, 0.1);
    transition: background-color 0.2s ease, transform 0.1s ease;
  }

  button:hover {
    background-color: #8e00e0;
  }

  button:active {
    transform: scale(0.98);
  }
</style>