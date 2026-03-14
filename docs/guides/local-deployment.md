# Deploy Locally

This guide walks you through running EnclaveStation on your own machine or a server on your local network using Docker Compose.

**Estimated time:** 5–10 minutes

## What you'll need

- **Docker** and **Docker Compose** — [Install Docker](https://docs.docker.com/get-docker/)
- **Git**

::: tip Supported platforms
Docker runs on Linux, macOS, and Windows. On macOS and Windows, install [Docker Desktop](https://www.docker.com/products/docker-desktop/). On Linux, install Docker Engine directly.
:::

## Step 1: Clone the repository

```bash
git clone --recurse-submodules https://github.com/dariusjlukas/enclave-station.git
cd enclave-station
```

If you already cloned without `--recurse-submodules`, initialize them now:

```bash
git submodule update --init --recursive
```

## Step 2: Configure environment variables

```bash
cp .env.example .env
```

Open `.env` in your editor and update the following:

```ini
# Change this to a strong, unique password
POSTGRES_PASSWORD=your-secure-password-here

# Set this to your machine's LAN IP so QR codes work for device linking
# (leave empty if you'll only use the app from this machine)
# Example: PUBLIC_URL=http://192.168.1.100
PUBLIC_URL=
```

::: details Full list of environment variables

| Variable | Default | Description |
|----------|---------|-------------|
| `POSTGRES_USER` | `chatapp` | PostgreSQL username |
| `POSTGRES_PASSWORD` | `changeme_in_production` | PostgreSQL password |
| `POSTGRES_DB` | `chatapp` | PostgreSQL database name |
| `SESSION_EXPIRY_HOURS` | `168` (7 days) | How long login sessions last |
| `PUBLIC_URL` | *(empty)* | Public-facing URL used for QR codes during device linking |
| `MAX_FILE_SIZE` | `1073741824` (1 GB) | Maximum upload file size in bytes |

:::

## Step 3: Build and start

```bash
docker compose up -d --build
```

The first build takes several minutes since it compiles the C++ backend from source. Subsequent starts are much faster.

Verify everything is running:

```bash
docker compose ps
```

You should see three services (`postgres`, `backend`, `frontend`) all showing as **running**.

## Step 4: Open the app

Open [http://localhost](http://localhost) in your browser. **The first user to register becomes the admin.**

## Accessing from other devices on your network

Other devices on the same local network (phones, tablets, other computers) can access EnclaveStation using your machine's LAN IP address.

### Find your LAN IP

::: code-group

```bash [Linux]
hostname -I | awk '{print $1}'
```

```bash [macOS]
ipconfig getifaddr en0
```

```powershell [Windows]
(Get-NetIPAddress -AddressFamily IPv4 -InterfaceAlias "Wi-Fi").IPAddress
```

:::

Then open `http://YOUR_LAN_IP` from any device on your network (e.g. `http://192.168.1.100`).

### Enable QR code device linking

For the device linking QR codes to work from other devices, set `PUBLIC_URL` in your `.env` to your LAN IP:

```ini
PUBLIC_URL=http://192.168.1.100
```

Then restart the backend:

```bash
docker compose restart backend
```

## Managing the server

### Viewing logs

```bash
docker compose logs -f           # All services
docker compose logs -f backend   # Backend only
docker compose logs -f frontend  # Nginx/frontend only
docker compose logs -f postgres  # Database only
```

### Stopping the server

```bash
docker compose down
```

This stops all containers but preserves your database and uploaded files.

### Updating to a new version

```bash
git pull --recurse-submodules
docker compose up -d --build
```

Your data persists across updates — it's stored in Docker volumes.

### Resetting the database

To wipe all data (users, messages, files) and start fresh:

```bash
docker compose down -v
docker compose up -d --build
```

::: warning
The `-v` flag deletes the PostgreSQL data volume and uploaded files. This cannot be undone. The first user to register after a reset becomes admin again.
:::

## Backups

### Database

Create a backup:

```bash
docker compose exec postgres pg_dump -U chatapp chatapp > backup-$(date +%Y%m%d).sql
```

Restore from a backup:

```bash
cat backup.sql | docker compose exec -T postgres psql -U chatapp chatapp
```

### Uploaded files

```bash
docker cp $(docker compose ps -q backend):/data/uploads ./uploads-backup
```

## Troubleshooting

### Port 80 is already in use

Another service (like Apache or Nginx) is using port 80. Either stop it, or change the frontend port mapping in `docker-compose.yml`:

```yaml
  frontend:
    ports:
      - "8080:80"  # Access via http://localhost:8080 instead
```

### Backend exits immediately

Check the logs:

```bash
docker compose logs backend
```

This usually means the database isn't ready or the credentials in `.env` don't match. Verify that the PostgreSQL container is healthy:

```bash
docker compose ps postgres
```

### Can't connect from other devices

1. Make sure your firewall allows incoming connections on port 80
2. Verify both devices are on the same network
3. Test with `curl http://YOUR_LAN_IP` from the other device

### Docker Compose version issues

If you see errors about `docker compose`, you may have the older standalone version. EnclaveStation requires Docker Compose V2 (the `docker compose` plugin, not the standalone `docker-compose` binary). Update Docker to get it.
