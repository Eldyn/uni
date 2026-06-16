#!/usr/bin/env bash
# One-shot provisioner for the UNI service on a fresh OCI ARM (Ampere A1) VM.
# Idempotent: safe to re-run. Targets Oracle Linux 9 (default OCI image) and
# Ubuntu 22.04/24.04. Run from the repo root after cloning:
#
#   DUCKDNS_SUBDOMAIN=myuni DUCKDNS_TOKEN=xxxx ./deploy/bootstrap.sh
#
# It installs Docker + compose, opens the host firewall, generates secrets,
# wires up the DuckDNS updater, and brings the Caddy + backend stack online.
# The OCI Security List ingress rules for 80/443 are NOT done here — they live
# in the cloud console and must be added separately (see deploy/README.md).

set -euo pipefail

# --- Config -----------------------------------------------------------------
DUCKDNS_SUBDOMAIN="${DUCKDNS_SUBDOMAIN:?set DUCKDNS_SUBDOMAIN (label only, e.g. myuni)}"
DUCKDNS_TOKEN="${DUCKDNS_TOKEN:?set DUCKDNS_TOKEN}"
DOMAIN="${DOMAIN:-${DUCKDNS_SUBDOMAIN}.duckdns.org}"

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

log() { printf '\n\033[1;36m== %s\033[0m\n' "$*"; }

# --- Distro detection -------------------------------------------------------
. /etc/os-release
case "$ID" in
	ol|rhel|centos|almalinux|rocky) FAMILY=rhel ;;
	ubuntu|debian)                  FAMILY=debian ;;
	*) echo "Unsupported distro: $ID" >&2; exit 1 ;;
esac
log "Detected $PRETTY_NAME ($FAMILY family), arch $(uname -m)"

# --- Docker engine + compose plugin -----------------------------------------
if ! command -v docker >/dev/null 2>&1; then
	log "Installing Docker"
	if [ "$FAMILY" = rhel ]; then
		sudo dnf -y install dnf-plugins-core
		sudo dnf config-manager --add-repo https://download.docker.com/linux/centos/docker-ce.repo
		sudo dnf -y install docker-ce docker-ce-cli containerd.io docker-compose-plugin
	else
		sudo apt-get update
		sudo apt-get -y install ca-certificates curl
		sudo install -m 0755 -d /etc/apt/keyrings
		sudo curl -fsSL https://download.docker.com/linux/ubuntu/gpg -o /etc/apt/keyrings/docker.asc
		sudo chmod a+r /etc/apt/keyrings/docker.asc
		echo "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/docker.asc] \
https://download.docker.com/linux/ubuntu $VERSION_CODENAME stable" \
			| sudo tee /etc/apt/sources.list.d/docker.list >/dev/null
		sudo apt-get update
		sudo apt-get -y install docker-ce docker-ce-cli containerd.io docker-compose-plugin
	fi
fi
sudo systemctl enable --now docker
sudo usermod -aG docker "$USER" || true  # takes effect on next login; we use sudo below

# --- Host firewall (port 80/443) --------------------------------------------
# Two worlds: firewalld (Oracle Linux) vs raw iptables (Ubuntu OCI images ship a
# REJECT rule). Open both ports in whichever is active. The OCI *Security List*
# is a separate cloud-side layer — see README.
log "Opening host firewall for 80/443"
if systemctl is-active --quiet firewalld; then
	sudo firewall-cmd --permanent --add-service=http
	sudo firewall-cmd --permanent --add-service=https
	sudo firewall-cmd --reload
else
	for port in 80 443; do
		if ! sudo iptables -C INPUT -p tcp --dport "$port" -j ACCEPT 2>/dev/null; then
			sudo iptables -I INPUT 1 -p tcp --dport "$port" -j ACCEPT
		fi
	done
	if [ "$FAMILY" = debian ]; then
		sudo apt-get -y install iptables-persistent
		sudo netfilter-persistent save
	else
		sudo sh -c 'iptables-save > /etc/sysconfig/iptables' || true
	fi
fi

# --- Secrets + runtime env (.env at repo root) ------------------------------
if [ ! -f .env ]; then
	log "Generating .env secrets"
	{
		printf 'JWT_SECRET=%s\n' "$(openssl rand -hex 32)"
		printf 'PASSWORD_PEPPER=%s\n' "$(openssl rand -hex 32)"
		printf 'DOMAIN=%s\n' "$DOMAIN"
		printf 'ACME_EMAIL=%s\n' "${ACME_EMAIL:-}"
	} > .env
	chmod 600 .env
else
	grep -q '^DOMAIN=' .env || printf 'DOMAIN=%s\n' "$DOMAIN" >> .env
	grep -q '^ACME_EMAIL=' .env || printf 'ACME_EMAIL=%s\n' "${ACME_EMAIL:-}" >> .env
fi
mkdir -p db_data

# --- DuckDNS updater (script + env + systemd timer) -------------------------
log "Installing DuckDNS updater"
sudo install -D -m 0755 deploy/duckdns.sh /opt/uni/duckdns.sh
sudo install -m 0600 /dev/stdin /etc/duckdns.env <<EOF
DUCKDNS_SUBDOMAIN=${DUCKDNS_SUBDOMAIN}
DUCKDNS_TOKEN=${DUCKDNS_TOKEN}
EOF
sudo install -m 0644 deploy/duckdns.service /etc/systemd/system/duckdns.service
sudo install -m 0644 deploy/duckdns.timer   /etc/systemd/system/duckdns.timer
sudo systemctl daemon-reload
sudo systemctl enable --now duckdns.timer
sudo systemctl start duckdns.service        # point the subdomain at us now
log "DuckDNS status:"; sudo systemctl status duckdns.service --no-pager -n 5 || true

# --- Build & launch ---------------------------------------------------------
log "Building image (native aarch64 — this takes a while) and starting stack"
sudo docker compose --project-directory . -f deploy/docker-compose.oci.yml up -d --build

log "Done. Stack status:"
sudo docker compose --project-directory . -f deploy/docker-compose.oci.yml ps
cat <<EOF

Next:
  * Ensure the OCI Security List allows ingress TCP 80 and 443 from 0.0.0.0/0.
  * Browse to https://${DOMAIN}  (Traefik fetches a Let's Encrypt cert on first hit;
    give it ~30s and watch 'sudo docker logs traefik' if it stalls).
EOF
