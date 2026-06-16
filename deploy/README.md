# Deploying UNI on an OCI ARM (Ampere A1) VM

Self-hosted deployment for Oracle Cloud's Always Free ARM instance. Caddy
terminates TLS for a free DuckDNS subdomain and reverse-proxies to the backend
(plain HTTP, `UNI_ENABLE_SSL=OFF` — same edge-termination shape as Render). The
SQLite database lives on the VM's persistent disk via a bind mount, so it
survives container recreation and reboots.

```
internet ──443──> Caddy (Let's Encrypt) ──3000──> uni backend ──> ./db_data/uni.sqlite
                  yourname.duckdns.org              (internal docker network)
```

## What you do in a browser (I can't automate these)

1. **OCI console** — launch the A1.Flex instance (Oracle Linux 9, aarch64),
   save the SSH key, note the **public IP**.
2. **OCI console → VCN → Security List (or NSG)** — add **ingress** rules:
   `0.0.0.0/0` → TCP **80** and TCP **443**. Without this, Let's Encrypt and all
   traffic time out no matter what the host firewall says.
3. **duckdns.org** — sign in, create a subdomain, copy the **token**.

## What `bootstrap.sh` does on the VM (over SSH)

Installs Docker + compose, opens the **host** firewall for 80/443, generates
`.env` secrets, installs the DuckDNS updater (systemd timer, every 5 min),
builds the image natively on aarch64, and brings the stack up.

```bash
git clone https://github.com/Eldyn/uni.git && cd uni
git checkout oci
DUCKDNS_SUBDOMAIN=yourname DUCKDNS_TOKEN=xxxxxxxx ./deploy/bootstrap.sh
```

`DOMAIN` defaults to `<subdomain>.duckdns.org`; override it by exporting `DOMAIN`.

## Verify

```bash
sudo docker compose --project-directory . -f deploy/docker-compose.oci.yml ps
sudo docker logs caddy   # watch the first cert issuance
curl -fsS https://yourname.duckdns.org/   # should return the frontend
```

Persistence check: register a user, `sudo docker compose ... restart uni`, log
back in — the account should still be there (it lives in `./db_data`).

## Day-2 operations

```bash
# Update to a new build after pulling
git pull && sudo docker compose --project-directory . -f deploy/docker-compose.oci.yml up -d --build

# Logs
sudo docker logs -f uni
sudo docker logs -f caddy

# Back up the database
sudo docker exec uni sqlite3 /app/data/uni.sqlite ".backup /app/data/backup.db"
```

## Notes / gotchas

- **Two firewalls.** The host firewall is handled by `bootstrap.sh`; the OCI
  Security List is **not** (it's cloud-side). Both must allow 80/443.
- **Build on the VM, not on WSL.** The image is `aarch64`; an x86 image built
  locally will not run on the A1.
- **TLS needs the DNS to resolve first.** The DuckDNS updater points the
  subdomain at the VM before Caddy requests a cert. If issuance fails, confirm
  `dig yourname.duckdns.org` returns the VM IP and that port 80 is open both
  cloud-side and host-side.
- **Backups aren't automatic.** The boot volume persists, but enable an OCI
  backup policy or cron the `.backup` above if the data matters.
