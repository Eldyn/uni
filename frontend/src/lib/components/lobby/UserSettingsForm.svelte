<script lang="ts">
    import { onMount } from "svelte";
    import { storeAuth } from "../../stores/auth.svelte";

    let userEmail = $state("utente@esempio.com");
    let winCount = $state(69); // idk mate
    let profileImage = $state("https://via.placeholder.com/100");

    function handleImageChange(event: Event) {
        const target = event.target as HTMLInputElement;
        if (target.files && target.files[0]) {
            const reader = new FileReader();
            reader.onload = (e) => {
                if (e.target?.result) {
                    profileImage = e.target.result as string;
                    // TODO: storeAuth.updateAvatar(profileImage)
                }
            };
            reader.readAsDataURL(target.files[0]);
        }
    }
</script>

<div class="settings-container">
    <div class="settings-card">
        <h2>Il tuo Profilo</h2>
        
        <div class="profile-avatar-section">
            <div class="avatar-wrapper">
                <img src={profileImage} alt="Avatar" class="avatar-preview" />
            </div>
            
            <label class="upload-btn">
                Cambia Immagine
                <input type="file" accept="image/*" onchange={handleImageChange} hidden />
            </label>
        </div>

        <div class="profile-info-grid">
            <div class="info-group">
                <label for="username">Nome Utente</label>
                <input type="text" id="username" value={storeAuth.username} disabled />
            </div>

            <div class="info-group">
                <label for="email">Email</label>
                <input type="email" id="email" bind:value={userEmail} disabled />
            </div>
        </div>

        <div class="stats-section">
            <h3>Statistiche di gioco</h3>
            <div class="stat-box">
                <span class="stat-label">Vittorie Totali:</span>
                <span class="stat-value">{winCount} 🏆</span>
            </div>
        </div>
    </div>
</div>

<style>
    .settings-container {
        display: flex;
        justify-content: center;
        margin-top: 20px;
    }

    .settings-card {
        background: var(--accent-bg, #f9f9f9);
        border: 1px solid var(--accent-border, #e2e8f0);
        padding: 32px;
        border-radius: 12px;
        width: 100%;
        max-width: 500px;
        box-shadow: 0 4px 6px -1px rgba(0,0,0,0.05);
    }

    .settings-card h2, h3 {
        margin-top: 0;
        margin-bottom: 24px;
        color: var(--text-h);
    }

    .profile-avatar-section {
        display: flex;
        flex-direction: column;
        align-items: center;
        gap: 12px;
        margin-bottom: 24px;
    }

    .avatar-wrapper {
        width: 100px;
        height: 100px;
        border-radius: 50%;
        border: 2px solid var(--accent, #a855f7);
        position: relative;
        display: flex;
        align-items: center;
        justify-content: center;
        background: rgba(255, 255, 255, 0.05);
        overflow: hidden;
    }

    .avatar-preview {
        width: 100%;
        height: 100%;
        object-fit: cover;
        display: flex;
        align-items: center;
        justify-content: center;
        text-align: center;
        font-size: 14px;
        color: var(--text, #fff);
    }

    .upload-btn {
        display: inline-flex;
        justify-content: center;
        align-items: center;
        padding: 6px 12px;
        background: var(--accent, #a855f7);
        color: white;
        font-size: 12px;
        border-radius: 4px;
        cursor: pointer;
    }

    .profile-info-grid {
        display: flex;
        flex-direction: column;
        gap: 16px;
        margin-bottom: 24px;
    }

    .info-group {
        display: flex;
        flex-direction: column;
        gap: 6px;
    }

    .info-group label {
        font-size: 14px;
        font-weight: 500;
        color:white;
    }

    .info-group input {
        padding: 10px;
        border: 1px solid var(--border, #cbd5e1);
        border-radius: 6px;
        background: white;
        color: black;
    }

    .info-group input:disabled {
        background: rgba(0,0,0,0.05);
        cursor: not-allowed;
        color:white;
    }

    .stats-section {
        border-top: 1px solid var(--border);
        padding-top: 16px;
    }

    .stat-box {
        display: flex;
        justify-content: space-between;
        align-items: center;
        padding: 12px;
        background: rgba(0,0,0,0.05);
        border-radius: 6px;
    }

    .stat-label {
        font-size: 14px;
        color: var(--text-muted, white);
        font-weight: 500;
    }

    .stat-value {
        font-weight: bold;
        color: var(--accent, #a855f7);
    }
</style>