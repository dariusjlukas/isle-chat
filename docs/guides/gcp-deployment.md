# Deploy on Google Cloud

This guide walks you through deploying EnclaveStation on a [Google Compute Engine](https://cloud.google.com/compute) VM instance. By the end, you'll have a running instance accessible over HTTPS.

**Estimated time:** 25–35 minutes

## What you'll need

- A **Google Cloud account** — [create one here](https://cloud.google.com/free) (includes $300 free credit for 90 days)
- A **domain name** (optional, but required for HTTPS)
- A local terminal with an SSH client

## Architecture overview

EnclaveStation runs as three Docker containers orchestrated by Docker Compose:

```
Internet ──► Nginx (:80/:443)
                ├── /           → React SPA (static files)
                ├── /api/*      → C++ backend (:9001)
                └── /ws         → WebSocket to backend (:9001)

Backend (:9001) ──► PostgreSQL (:5432)
```

Everything runs on a single Compute Engine VM.

## Step 1: Create a VM instance

### Using the Cloud Console

1. Open the [Compute Engine console](https://console.cloud.google.com/compute/instances)
2. Click **Create instance**
3. Configure the instance:

   | Setting | Value |
   |---------|-------|
   | **Name** | `enclave-station` |
   | **Region/Zone** | Choose one close to your users |
   | **Machine type** | See table below |
   | **Boot disk** | Ubuntu 24.04 LTS, 20 GB balanced persistent disk |

   **Machine type recommendations:**

   | Type | vCPUs | RAM | Best for | Cost (approx.) |
   |------|-------|-----|----------|----------------|
   | `e2-micro` | 0.25 | 1 GB | Testing only (free tier) | Free |
   | `e2-small` | 0.5 | 2 GB | 1–10 users, light usage | ~$13/mo |
   | `e2-medium` | 1 | 4 GB | 10–50 users, recommended | ~$25/mo |
   | `e2-standard-2` | 2 | 8 GB | 50+ users or heavy file uploads | ~$49/mo |

   ::: tip Free tier
   Google Cloud's free tier includes one `e2-micro` instance per month (in select US regions) indefinitely. It's very limited but enough to test with.
   :::

4. Under **Firewall**, check:
   - **Allow HTTP traffic**
   - **Allow HTTPS traffic**

5. Click **Create**

::: warning
Do **not** create firewall rules for port 5432 (PostgreSQL) or 9001 (backend). These should only be accessible internally between containers.
:::

### Using the gcloud CLI

```bash
# Create the instance
gcloud compute instances create enclave-station \
  --zone=us-central1-a \
  --machine-type=e2-medium \
  --image-family=ubuntu-2404-lts-amd64 \
  --image-project=ubuntu-os-cloud \
  --boot-disk-size=20GB \
  --tags=http-server,https-server

# The http-server and https-server tags automatically allow ports 80 and 443
# If the default firewall rules don't exist, create them:
gcloud compute firewall-rules create allow-http \
  --direction=INGRESS --action=ALLOW --rules=tcp:80 --target-tags=http-server 2>/dev/null || true

gcloud compute firewall-rules create allow-https \
  --direction=INGRESS --action=ALLOW --rules=tcp:443 --target-tags=https-server 2>/dev/null || true
```

## Step 2: Reserve a static IP

By default, external IPs are ephemeral and may change on restart.

### Console

1. Go to **VPC Network** → **IP addresses**
2. Find your instance's IP → click **Reserve** under the **Type** column

### CLI

```bash
# Reserve a static IP
gcloud compute addresses create enclave-station-ip \
  --region=us-central1

# Get the IP
gcloud compute addresses describe enclave-station-ip \
  --region=us-central1 --format='value(address)'

# Assign it to your instance
gcloud compute instances delete-access-config enclave-station \
  --zone=us-central1-a --access-config-name="External NAT"

gcloud compute instances add-access-config enclave-station \
  --zone=us-central1-a \
  --address=$(gcloud compute addresses describe enclave-station-ip --region=us-central1 --format='value(address)')
```

Note down the static IP — you'll need it for DNS.

## Step 3: Connect via SSH

The easiest way is using gcloud:

```bash
gcloud compute ssh enclave-station --zone=us-central1-a
```

Or use the **SSH** button in the Cloud Console on your instance's row.

You can also use a standard SSH client with the key at `~/.ssh/google_compute_engine` (created by `gcloud compute ssh` on first use).

## Step 4: Install Docker

```bash
# Update system packages
sudo apt update && sudo apt upgrade -y

# Install Docker using the official convenience script
curl -fsSL https://get.docker.com | sudo sh

# Add your user to the docker group
sudo usermod -aG docker $USER

# Apply group change
newgrp docker

# Verify
docker --version
docker compose version
```

## Step 5: Clone and configure

```bash
# Clone the repository with submodules
git clone --recurse-submodules https://github.com/dariusjlukas/enclave-station.git
cd enclave-station

# Create your environment file from the example
cp .env.example .env
```

Edit the `.env` file:

```bash
nano .env
```

```ini
# IMPORTANT: Change this to a strong, unique password
POSTGRES_PASSWORD=your-secure-password-here

# Set this to your domain or static IP
PUBLIC_URL=https://chat.example.com
```

::: tip Generating a strong password
```bash
openssl rand -base64 24
```
:::

## Step 6: Build and start

```bash
docker compose up -d --build
```

The first build takes several minutes. Verify everything is running:

```bash
docker compose ps
```

You should see three services all showing as **running**. Visit `http://YOUR_STATIC_IP` to confirm. **The first user to register becomes the admin.**

## Step 7: Set up HTTPS with Let's Encrypt

### Point your domain to the server

Create a DNS **A record** pointing to your static IP:

| Type | Name | Value |
|------|------|-------|
| A | `chat` (or `@` for root domain) | `YOUR_STATIC_IP` |

Verify propagation:

```bash
dig +short chat.example.com
```

### Obtain a certificate

```bash
# Install certbot
sudo apt install -y certbot

# Stop containers so certbot can use port 80
docker compose down

# Obtain a certificate
sudo certbot certonly --standalone -d chat.example.com
```

### Configure Nginx for SSL

```bash
cat > nginx/nginx-ssl.conf << 'NGINX'
server {
    listen 80;
    server_name _;
    return 301 https://$host$request_uri;
}

server {
    listen 443 ssl;
    server_name _;

    ssl_certificate     /etc/letsencrypt/live/chat.example.com/fullchain.pem;
    ssl_certificate_key /etc/letsencrypt/live/chat.example.com/privkey.pem;

    ssl_protocols TLSv1.2 TLSv1.3;
    ssl_ciphers HIGH:!aNULL:!MD5;

    root /usr/share/nginx/html;
    index index.html;

    # API proxy
    location /api/ {
        proxy_pass http://backend:9001;
        proxy_http_version 1.1;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
        client_max_body_size 0;
        proxy_read_timeout 3600;
        proxy_send_timeout 3600;
        proxy_connect_timeout 60;
    }

    # WebSocket proxy
    location /ws {
        proxy_pass http://backend:9001;
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection "upgrade";
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_read_timeout 86400;
    }

    # SPA fallback
    location / {
        try_files $uri $uri/ /index.html;
    }
}
NGINX
```

Update the `frontend` service in `docker-compose.yml`:

```bash
nano docker-compose.yml
```

Replace the `frontend` service with:

```yaml
  frontend:
    build:
      context: ./frontend
      dockerfile: Dockerfile
    volumes:
      - ./nginx/nginx-ssl.conf:/etc/nginx/conf.d/default.conf:z
      - /etc/letsencrypt:/etc/letsencrypt:ro
    ports:
      - "80:80"
      - "443:443"
    depends_on:
      - backend
```

Start everything:

```bash
docker compose up -d --build
```

### Automatic certificate renewal

```bash
sudo crontab -e
```

Add:

```
0 3 * * * certbot renew --pre-hook "cd /home/$USER/enclave-station && docker compose stop frontend" --post-hook "cd /home/$USER/enclave-station && docker compose start frontend" >> /var/log/certbot-renew.log 2>&1
```

## Updating EnclaveStation

```bash
cd ~/enclave-station
git pull --recurse-submodules
docker compose up -d --build
```

## Backups

### Database

```bash
docker compose exec postgres pg_dump -U chatapp chatapp > backup-$(date +%Y%m%d).sql
```

### Uploaded files

```bash
docker cp $(docker compose ps -q backend):/data/uploads ./uploads-backup
```

### Automating backups

```bash
mkdir -p ~/backups
sudo crontab -e
```

```
0 2 * * * cd /home/$USER/enclave-station && docker compose exec -T postgres pg_dump -U chatapp chatapp | gzip > /home/$USER/backups/db-$(date +\%Y\%m\%d).sql.gz 2>&1
```

::: tip Offsite backups with Cloud Storage
```bash
# Install gsutil (included with gcloud CLI)
gsutil mb gs://your-backup-bucket
gsutil -m rsync -r ~/backups gs://your-backup-bucket/enclave-station/
```
:::

## Troubleshooting

### Can't connect from the browser

1. Verify firewall rules allow HTTP/HTTPS:
   ```bash
   gcloud compute firewall-rules list --filter="targetTags:http-server OR targetTags:https-server"
   ```
2. Check containers are running: `docker compose ps`
3. Test locally: `curl -I http://localhost`

### VM runs out of memory

Check for OOM events:

```bash
dmesg | grep -i "oom\|killed"
```

Resize the VM: stop the instance, change the machine type, then start it again.

```bash
gcloud compute instances stop enclave-station --zone=us-central1-a
gcloud compute instances set-machine-type enclave-station \
  --zone=us-central1-a --machine-type=e2-standard-2
gcloud compute instances start enclave-station --zone=us-central1-a
```

### Disk space issues

```bash
df -h
docker system df
docker system prune -a
```

To expand the boot disk:

```bash
gcloud compute disks resize enclave-station --zone=us-central1-a --size=40GB
# Then inside the VM:
sudo growpart /dev/sda 1
sudo resize2fs /dev/sda1
```

## Costs

| Resource | Cost (approx.) |
|----------|----------------|
| `e2-medium` VM | ~$25/mo |
| 20 GB balanced disk | ~$2/mo |
| Static IP (attached) | Free |
| Data transfer (first 200 GB/mo) | Free |
| Let's Encrypt SSL | Free |
| **Total** | **~$27/mo** |

Costs vary by region. Use the [Google Cloud Pricing Calculator](https://cloud.google.com/products/calculator) for exact estimates. Consider [Committed Use Discounts](https://cloud.google.com/compute/docs/instances/committed-use-discounts-overview) (1 or 3 year) for savings up to 57%.
