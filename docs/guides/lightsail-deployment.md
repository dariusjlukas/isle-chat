# Deploy on AWS Lightsail

This guide walks you through deploying EnclaveStation on an [AWS Lightsail](https://aws.amazon.com/lightsail/) virtual server. By the end, you'll have a running instance accessible over HTTPS with your own domain.

**Estimated time:** 20–30 minutes

## What you'll need

- An **AWS account** — [create one here](https://aws.amazon.com/) if you don't have one
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

Everything runs on a single Lightsail instance. PostgreSQL data and uploaded files are stored in Docker volumes that persist across container restarts.

## Step 1: Create a Lightsail instance

1. Open the [Lightsail console](https://lightsail.aws.amazon.com/)
2. Click **Create instance**
3. Choose your preferred **region** (pick one close to your users)
4. Under **Platform**, select **Linux/Unix**
5. Under **Blueprint**, select **OS Only**, then **Ubuntu 24.04 LTS**
6. Choose an instance plan:

   | Plan | RAM | vCPUs | Best for |
   |------|-----|-------|----------|
   | $5/mo | 1 GB | 2 | 1–10 users, light usage |
   | $10/mo | 2 GB | 2 | 10–50 users, recommended starting point |
   | $20/mo | 4 GB | 2 | 50+ users or heavy file uploads |

   ::: tip
   The $10/mo plan is a good starting point for most deployments. You can resize later without data loss.
   :::

7. Give your instance a name (e.g. `enclave-station`)
8. Click **Create instance**

Wait for the instance status to show **Running** (usually under a minute).

## Step 2: Attach a static IP

By default, your instance gets a dynamic IP that can change on reboot. A static IP is free while attached to a running instance.

1. Go to the **Networking** tab of your instance
2. Under **Public IPv4**, click **Attach static IP**
3. Name it (e.g. `enclave-station-ip`) and click **Create and attach**

Note down the static IP — you'll need it for DNS configuration.

## Step 3: Configure the firewall

Lightsail's firewall only allows SSH (port 22) by default. You need to open HTTP and HTTPS.

1. On your instance page, go to the **Networking** tab
2. Under **IPv4 Firewall**, click **Add rule**:
   - **Application:** HTTP, **Port:** 80
3. Click **Add rule** again:
   - **Application:** HTTPS, **Port:** 443
4. Click **Save**

::: warning
Do **not** open port 5432 (PostgreSQL) or 9001 (backend). These should only be accessible internally between containers.
:::

## Step 4: Connect via SSH

You can connect in two ways:

**Option A: Browser-based SSH** (quickest)

Click the terminal icon on your instance in the Lightsail console.

**Option B: Your own SSH client** (recommended for copy-pasting commands)

1. Go to your instance → **Connect** tab → **Download default key**
2. Connect:
   ```bash
   chmod 400 ~/Downloads/LightsailDefaultKey-*.pem
   ssh -i ~/Downloads/LightsailDefaultKey-*.pem ubuntu@YOUR_STATIC_IP
   ```

## Step 5: Install Docker

Run these commands on the Lightsail instance:

```bash
# Update system packages
sudo apt update && sudo apt upgrade -y

# Install Docker using the official convenience script
curl -fsSL https://get.docker.com | sudo sh

# Add your user to the docker group (avoids needing sudo for docker commands)
sudo usermod -aG docker $USER

# Apply group change (or log out and back in)
newgrp docker

# Verify Docker is working
docker --version
docker compose version
```

## Step 6: Clone and configure

```bash
# Clone the repository with submodules
git clone --recurse-submodules https://github.com/dariusjlukas/enclave-station.git
cd enclave-station

# Create your environment file from the example
cp .env.example .env
```

Now edit the `.env` file with secure values:

```bash
nano .env
```

Update the following:

```ini
# IMPORTANT: Change this to a strong, unique password
POSTGRES_PASSWORD=your-secure-password-here

# Set this to your domain or static IP so QR codes work for device linking
# Examples:
#   PUBLIC_URL=https://chat.example.com
#   PUBLIC_URL=http://YOUR_STATIC_IP
PUBLIC_URL=https://chat.example.com
```

::: tip Generating a strong password
Run this on the server to generate a random password:
```bash
openssl rand -base64 24
```
:::

Save and exit nano (`Ctrl+X`, then `Y`, then `Enter`).

## Step 7: Build and start

```bash
docker compose up -d --build
```

The first build takes several minutes since it compiles the C++ backend from source. Subsequent starts will be much faster.

Verify everything is running:

```bash
docker compose ps
```

You should see three services (`postgres`, `backend`, `frontend`) all showing as **Up** (or **running**).

At this point, EnclaveStation is accessible at `http://YOUR_STATIC_IP`. Open it in a browser to verify. **The first user to register becomes the admin.**

## Step 8: Set up a domain with HTTPS

Running over plain HTTP works, but HTTPS is strongly recommended for a production deployment. This section uses [Let's Encrypt](https://letsencrypt.org/) for free TLS certificates.

### Point your domain to the server

In your domain registrar's DNS settings, create an **A record**:

| Type | Name | Value |
|------|------|-------|
| A | `chat` (or `@` for root domain) | `YOUR_STATIC_IP` |

Wait for DNS propagation (usually a few minutes, can take up to 48 hours). You can verify with:

```bash
dig +short chat.example.com
```

It should return your static IP.

### Install Certbot and obtain a certificate

Back on your Lightsail instance:

```bash
# Install certbot
sudo apt install -y certbot

# Stop the running containers temporarily so certbot can use port 80
docker compose down

# Obtain a certificate (replace with your actual domain)
sudo certbot certonly --standalone -d chat.example.com
```

Follow the prompts to accept the terms and provide an email for renewal notices. Certbot will place your certificates in `/etc/letsencrypt/live/chat.example.com/`.

### Configure Nginx for HTTPS

Create an updated Nginx configuration that handles SSL:

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

Update `docker-compose.yml` to mount the SSL certificates and use the new config. Edit the `frontend` service:

```bash
nano docker-compose.yml
```

Replace the `frontend` service section with:

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

Start everything back up:

```bash
docker compose up -d --build
```

Visit `https://chat.example.com` — you should see a valid HTTPS connection.

### Set up automatic certificate renewal

Let's Encrypt certificates expire every 90 days. Set up a cron job to renew automatically:

```bash
sudo crontab -e
```

Add this line:

```
0 3 * * * certbot renew --pre-hook "cd /home/ubuntu/enclave-station && docker compose stop frontend" --post-hook "cd /home/ubuntu/enclave-station && docker compose start frontend" >> /var/log/certbot-renew.log 2>&1
```

This checks for renewal daily at 3 AM. It only restarts the frontend container when a certificate is actually renewed.

## Updating EnclaveStation

When a new version is released:

```bash
cd ~/enclave-station

# Pull the latest code
git pull --recurse-submodules

# Rebuild and restart
docker compose up -d --build
```

Your data (database, uploaded files) is stored in Docker volumes and will persist across updates.

## Backups

### Database

Create a database dump:

```bash
docker compose exec postgres pg_dump -U chatapp chatapp > backup-$(date +%Y%m%d).sql
```

Restore from a backup:

```bash
cat backup-20260313.sql | docker compose exec -T postgres psql -U chatapp chatapp
```

### Uploaded files

The uploads volume can be backed up by copying from the Docker volume:

```bash
docker cp $(docker compose ps -q backend):/data/uploads ./uploads-backup
```

### Automating backups

Add a cron job for daily database backups:

```bash
sudo crontab -e
```

```
0 2 * * * cd /home/ubuntu/enclave-station && docker compose exec -T postgres pg_dump -U chatapp chatapp | gzip > /home/ubuntu/backups/db-$(date +\%Y\%m\%d).sql.gz 2>&1
```

Create the backups directory first:

```bash
mkdir -p ~/backups
```

::: tip Offsite backups
For extra safety, consider syncing your backups to S3:
```bash
# Install the AWS CLI
sudo apt install -y awscli

# Sync backups to an S3 bucket
aws s3 sync ~/backups s3://your-backup-bucket/enclave-station/
```
:::

## Troubleshooting

### Containers won't start

Check the logs for errors:

```bash
docker compose logs
```

Common issues:
- **Backend exits immediately**: Usually a database connection issue. Verify PostgreSQL is healthy with `docker compose ps` and check that your `.env` credentials match.
- **Port 80 already in use**: Another process is using port 80. Check with `sudo lsof -i :80` and stop it.

### Can't connect from the browser

1. Verify the firewall rules in the Lightsail console (Step 3)
2. Check that containers are running: `docker compose ps`
3. Test locally on the server: `curl -I http://localhost`

### WebSocket connection fails

If the app loads but messages don't appear in real time:
- Ensure your reverse proxy / load balancer (if any) supports WebSocket upgrades
- Check that port 443 is open in the Lightsail firewall
- Look for WebSocket errors in `docker compose logs backend`

### Running out of disk space

Check disk usage:

```bash
df -h
docker system df
```

Clean up unused Docker resources:

```bash
docker system prune -a
```

## Costs

| Resource | Cost |
|----------|------|
| Lightsail instance ($10 plan) | $10/mo |
| Static IP (attached to running instance) | Free |
| Data transfer (first 3 TB) | Included |
| Let's Encrypt SSL | Free |
| **Total** | **~$10/mo** |

Lightsail pricing is predictable — no surprise bandwidth or IOPS charges.
