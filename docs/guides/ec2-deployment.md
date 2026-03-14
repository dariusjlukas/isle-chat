# Deploy on AWS EC2

This guide walks you through deploying EnclaveStation on an [AWS EC2](https://aws.amazon.com/ec2/) instance. EC2 gives you more control over instance types, networking, and storage compared to [Lightsail](/guides/lightsail-deployment), but requires a bit more configuration.

**Estimated time:** 25–35 minutes

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

Everything runs on a single EC2 instance.

## Step 1: Launch an EC2 instance

1. Open the [EC2 console](https://console.aws.amazon.com/ec2/)
2. Click **Launch instances**
3. Configure the instance:

   | Setting | Value |
   |---------|-------|
   | **Name** | `enclave-station` |
   | **AMI** | Ubuntu Server 24.04 LTS (64-bit x86) |
   | **Instance type** | See table below |
   | **Key pair** | Create a new key pair or select an existing one |

   **Instance type recommendations:**

   | Type | vCPUs | RAM | Best for | Cost (approx.) |
   |------|-------|-----|----------|----------------|
   | `t3.micro` | 2 | 1 GB | 1–10 users, light usage | ~$8/mo |
   | `t3.small` | 2 | 2 GB | 10–50 users, recommended | ~$15/mo |
   | `t3.medium` | 2 | 4 GB | 50+ users or heavy file uploads | ~$30/mo |

   ::: tip Free tier
   If your AWS account is less than 12 months old, `t2.micro` (1 vCPU, 1 GB RAM) is free-tier eligible — enough for trying things out with a few users.
   :::

4. Under **Network settings**, click **Edit** and configure:
   - **Auto-assign public IP:** Enable
   - Under **Security group rules**, ensure you have:
     - SSH (port 22) from your IP
     - HTTP (port 80) from anywhere (`0.0.0.0/0`)
     - HTTPS (port 443) from anywhere (`0.0.0.0/0`)

5. Under **Configure storage**, set the root volume to at least **20 GB** (gp3)

6. Click **Launch instance**

::: warning
Do **not** open port 5432 (PostgreSQL) or 9001 (backend). These should only be accessible internally between containers.
:::

## Step 2: Allocate an Elastic IP

A public IP assigned at launch can change if the instance is stopped. An Elastic IP is free while attached to a running instance.

1. In the EC2 console, go to **Elastic IPs** (under Network & Security)
2. Click **Allocate Elastic IP address** → **Allocate**
3. Select the new Elastic IP → **Actions** → **Associate Elastic IP address**
4. Select your `enclave-station` instance → **Associate**

Note down the Elastic IP — you'll need it for DNS.

## Step 3: Connect via SSH

```bash
chmod 400 ~/Downloads/your-key-pair.pem
ssh -i ~/Downloads/your-key-pair.pem ubuntu@YOUR_ELASTIC_IP
```

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

# Set this to your domain or Elastic IP
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

You should see three services all showing as **running**. Visit `http://YOUR_ELASTIC_IP` to confirm. **The first user to register becomes the admin.**

## Step 7: Set up HTTPS with Let's Encrypt

### Point your domain to the server

Create a DNS **A record** pointing to your Elastic IP:

| Type | Name | Value |
|------|------|-------|
| A | `chat` (or `@` for root domain) | `YOUR_ELASTIC_IP` |

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
0 3 * * * certbot renew --pre-hook "cd /home/ubuntu/enclave-station && docker compose stop frontend" --post-hook "cd /home/ubuntu/enclave-station && docker compose start frontend" >> /var/log/certbot-renew.log 2>&1
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
0 2 * * * cd /home/ubuntu/enclave-station && docker compose exec -T postgres pg_dump -U chatapp chatapp | gzip > /home/ubuntu/backups/db-$(date +\%Y\%m\%d).sql.gz 2>&1
```

::: tip Offsite backups with S3
```bash
sudo apt install -y awscli
aws s3 sync ~/backups s3://your-backup-bucket/enclave-station/
```
:::

## Troubleshooting

### Can't connect from the browser

1. Check the EC2 **Security Group** allows inbound traffic on ports 80 and 443
2. Verify containers are running: `docker compose ps`
3. Test locally: `curl -I http://localhost`

### Instance runs out of memory

If the backend or PostgreSQL gets OOM-killed, your instance type may be too small. Check with:

```bash
dmesg | grep -i "oom\|killed"
```

Upgrade to a larger instance type via the EC2 console (requires a stop/start).

### Disk space issues

```bash
df -h
docker system df
docker system prune -a
```

## Lightsail vs EC2

| | Lightsail | EC2 |
|---|---|---|
| **Pricing** | Fixed monthly ($5–$160) | Pay-per-hour, variable |
| **Networking** | Bundled transfer | Separate charges |
| **Flexibility** | Limited instance types | Full instance catalog |
| **Best for** | Simple, predictable deployments | Custom networking, scaling needs |

If you want the simplest setup with predictable costs, consider [deploying on Lightsail](/guides/lightsail-deployment) instead.

## Costs

| Resource | Cost (approx.) |
|----------|----------------|
| EC2 `t3.small` instance | ~$15/mo |
| Elastic IP (attached) | Free |
| 20 GB gp3 storage | ~$1.60/mo |
| Data transfer (first 100 GB/mo) | Free |
| Let's Encrypt SSL | Free |
| **Total** | **~$17/mo** |

Costs vary by region. Use the [AWS Pricing Calculator](https://calculator.aws/) for exact estimates.
