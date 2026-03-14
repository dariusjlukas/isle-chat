# Deploy on Azure

This guide walks you through deploying EnclaveStation on an [Azure Virtual Machine](https://azure.microsoft.com/en-us/products/virtual-machines/). By the end, you'll have a running instance accessible over HTTPS.

**Estimated time:** 25–35 minutes

## What you'll need

- An **Azure account** — [create one here](https://azure.microsoft.com/en-us/free/) (includes $200 free credit for 30 days)
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

Everything runs on a single Azure VM.

## Step 1: Create a Virtual Machine

### Using the Azure Portal

1. Open the [Azure Portal](https://portal.azure.com/)
2. Click **Create a resource** → **Virtual Machine** → **Create**
3. Configure the **Basics** tab:

   | Setting | Value |
   |---------|-------|
   | **Resource group** | Create new: `enclave-station-rg` |
   | **Virtual machine name** | `enclave-station` |
   | **Region** | Choose one close to your users |
   | **Image** | Ubuntu Server 24.04 LTS - x64 Gen2 |
   | **Size** | See table below |
   | **Authentication type** | SSH public key |
   | **Username** | `azureuser` |

   **VM size recommendations:**

   | Size | vCPUs | RAM | Best for | Cost (approx.) |
   |------|-------|-----|----------|----------------|
   | `Standard_B1s` | 1 | 1 GB | 1–10 users, light usage | ~$8/mo |
   | `Standard_B2s` | 2 | 4 GB | 10–50 users, recommended | ~$31/mo |
   | `Standard_B2ms` | 2 | 8 GB | 50+ users or heavy file uploads | ~$61/mo |

   ::: tip Free tier
   Azure's free tier includes 750 hours/month of `Standard_B1s` for the first 12 months.
   :::

4. On the **Disks** tab, keep the default 30 GB Premium SSD

5. On the **Networking** tab:
   - A new virtual network and public IP will be created automatically
   - Under **NIC network security group**, select **Advanced**, then click **Create new**
   - Add inbound rules for:
     - **HTTP**: Port 80, Source: Any
     - **HTTPS**: Port 443, Source: Any
   - SSH (port 22) is added by default

6. Click **Review + create** → **Create**

7. When prompted, **download the private key** file (`.pem`)

::: warning
Do **not** open port 5432 (PostgreSQL) or 9001 (backend). These should only be accessible internally between containers.
:::

### Using the Azure CLI

Alternatively, create everything with a single command:

```bash
az vm create \
  --resource-group enclave-station-rg \
  --name enclave-station \
  --image Ubuntu2404 \
  --size Standard_B2s \
  --admin-username azureuser \
  --generate-ssh-keys \
  --public-ip-sku Standard \
  --nsg-rule SSH

# Open HTTP and HTTPS ports
az vm open-port --resource-group enclave-station-rg --name enclave-station --port 80 --priority 1001
az vm open-port --resource-group enclave-station-rg --name enclave-station --port 443 --priority 1002
```

## Step 2: Get your public IP

### Portal

Go to your VM's **Overview** page — the public IP is shown under **Public IP address**.

### CLI

```bash
az vm show -d --resource-group enclave-station-rg --name enclave-station --query publicIps -o tsv
```

Note this IP — you'll need it for DNS and SSH.

::: tip Static IP
By default, Azure assigns a static public IP when using Standard SKU. If you used Basic SKU, go to the **Public IP address** resource and set the **Assignment** to **Static** to prevent it from changing.
:::

## Step 3: Connect via SSH

```bash
chmod 400 ~/Downloads/enclave-station_key.pem
ssh -i ~/Downloads/enclave-station_key.pem azureuser@YOUR_PUBLIC_IP
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

# Set this to your domain or public IP
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

You should see three services all showing as **running**. Visit `http://YOUR_PUBLIC_IP` to confirm. **The first user to register becomes the admin.**

## Step 7: Set up HTTPS with Let's Encrypt

### Point your domain to the server

Create a DNS **A record** pointing to your Azure VM's public IP:

| Type | Name | Value |
|------|------|-------|
| A | `chat` (or `@` for root domain) | `YOUR_PUBLIC_IP` |

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
0 3 * * * certbot renew --pre-hook "cd /home/azureuser/enclave-station && docker compose stop frontend" --post-hook "cd /home/azureuser/enclave-station && docker compose start frontend" >> /var/log/certbot-renew.log 2>&1
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
0 2 * * * cd /home/azureuser/enclave-station && docker compose exec -T postgres pg_dump -U chatapp chatapp | gzip > /home/azureuser/backups/db-$(date +\%Y\%m\%d).sql.gz 2>&1
```

::: tip Offsite backups with Azure Blob Storage
```bash
# Install the Azure CLI
curl -sL https://aka.ms/InstallAzureCLIDeb | sudo bash

# Upload backups
az storage blob upload-batch \
  --destination your-container \
  --account-name yourstorageaccount \
  --source ~/backups
```
:::

## Troubleshooting

### Can't connect from the browser

1. Check the **Network Security Group** (NSG) rules allow inbound traffic on ports 80 and 443
2. Verify containers are running: `docker compose ps`
3. Test locally: `curl -I http://localhost`

To check NSG rules via CLI:

```bash
az network nsg rule list --resource-group enclave-station-rg --nsg-name enclave-stationNSG -o table
```

### VM runs out of memory

Check for OOM events:

```bash
dmesg | grep -i "oom\|killed"
```

Resize the VM via the portal: **VM** → **Size** → select a larger size → **Resize**. This requires a restart.

### Disk space issues

```bash
df -h
docker system df
docker system prune -a
```

## Costs

| Resource | Cost (approx.) |
|----------|----------------|
| `Standard_B2s` VM | ~$31/mo |
| 30 GB Premium SSD | ~$5/mo |
| Public IP (static) | ~$4/mo |
| Data transfer (first 100 GB/mo) | ~$8/mo |
| Let's Encrypt SSL | Free |
| **Total** | **~$48/mo** |

Costs vary by region. Use the [Azure Pricing Calculator](https://azure.microsoft.com/en-us/pricing/calculator/) for exact estimates. Consider [Reserved Instances](https://azure.microsoft.com/en-us/pricing/reserved-vm-instances/) (1 or 3 year commitment) for significant savings.
