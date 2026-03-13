# EnclaveStation

A self-hosted chat application with PKI-based authentication, multi-device support, and real-time messaging over WebSockets.

- **Backend**: C++ (uWebSockets, libpqxx, nlohmann/json)
- **Frontend**: React, TypeScript, Tailwind CSS
- **Database**: PostgreSQL 16
- **Proxy**: Nginx (serves frontend + proxies API/WebSocket to backend)

## Quick Start (Docker)

### Prerequisites

- Docker and Docker Compose

### Steps

1. Clone the repository with submodules:

   ```
   git clone --recurse-submodules https://github.com/your-org/enclave-station.git
   cd enclave-station
   ```

   If you already cloned without `--recurse-submodules`, initialize them now:

   ```
   git submodule update --init --recursive
   ```

2. Copy the example environment file and edit it:

   ```
   cp .env.example .env
   ```

   At minimum, change `POSTGRES_PASSWORD` to something secure.

3. Build and start:

   ```
   docker compose up -d --build
   ```

4. Open `http://localhost` in your browser. The first user to register becomes the admin.

## Development Setup

### Prerequisites

- **Node.js** 22+ and npm
- **CMake** 3.16+
- **C++ compiler** with C++17 support (GCC 9+ or Clang 10+)
- **PostgreSQL client libraries** (`libpqxx-devel` / `libpqxx-dev`)
- **OpenSSL development headers** (`openssl-devel` / `libssl-dev`)
- **zlib** (`zlib-devel` / `zlib1g-dev`)
- **Docker** (for running the test PostgreSQL container and building images)
- **Python 3** with `venv` (for API tests)

### Getting Started

1. Clone with submodules:

   ```
   git clone --recurse-submodules https://github.com/your-org/enclave-station.git
   cd enclave-station
   ```

2. Install frontend dependencies:

   ```
   cd frontend && npm install && cd ..
   ```

3. Install E2E test dependencies and Playwright browsers:

   ```
   cd tests/e2e && npm install && npx playwright install chromium && cd ../..
   ```

4. Build the backend:

   ```
   cd backend && cmake -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON && cmake --build build -j$(nproc) && cd ..
   ```

5. Run the full test suite:

   ```
   ./run-tests.sh
   ```

   The test runner handles starting/stopping a PostgreSQL container, backend servers, and Vite dev servers automatically.

### Running Individual Components

**Frontend dev server:**

```
cd frontend && npm run dev
```

**Backend server** (requires a running PostgreSQL instance):

```
POSTGRES_HOST=localhost POSTGRES_PORT=5432 POSTGRES_USER=chatapp \
POSTGRES_PASSWORD=changeme POSTGRES_DB=chatapp \
./backend/build/chat-server
```

### Running Specific Tests

The test runner supports targeted test execution:

```
./run-tests.sh --frontend         # Lint, typecheck, format, build
./run-tests.sh --backend          # Build + unit + integration tests
./run-tests.sh --backend-unit     # Backend unit tests only
./run-tests.sh --api-tests        # Black-box API tests
./run-tests.sh --e2e              # Playwright E2E tests
./run-tests.sh --docker           # Docker image builds
./run-tests.sh --e2e --parallel 4 # E2E tests with 4 parallel workers
./run-tests.sh --help             # Full list of options
```

## Configuration

All configuration is done through environment variables in `.env`:

| Variable | Default | Description |
|---|---|---|
| `POSTGRES_USER` | `chatapp` | PostgreSQL username |
| `POSTGRES_PASSWORD` | `changeme_in_production` | PostgreSQL password |
| `POSTGRES_DB` | `chatapp` | PostgreSQL database name |
| `SESSION_EXPIRY_HOURS` | `168` (7 days) | How long login sessions last |
| `PUBLIC_URL` | *(empty)* | Public-facing URL for QR codes (e.g. `http://192.168.1.100`) |

## Usage

### Starting the server

```
docker compose up -d
```

Add `--build` if you've made code changes:

```
docker compose up -d --build
```

### Stopping the server

```
docker compose down
```

This stops all containers but preserves the database volume.

### Viewing logs

```
docker compose logs -f           # all services
docker compose logs -f backend   # backend only
docker compose logs -f frontend  # nginx/frontend only
docker compose logs -f postgres  # database only
```

### Resetting the database

To wipe all data (users, messages, channels) and start fresh:

```
docker compose down -v
docker compose up -d --build
```

The `-v` flag removes the PostgreSQL data volume. The next startup will run migrations and create a clean database. The first user to register will become admin again.

### Accessing from other devices

The server listens on port 80. Other devices on the same network can access it via your machine's IP address (e.g. `http://192.168.1.100`).

To find your machine's IP:

```
# Linux
hostname -I | awk '{print $1}'

# macOS
ipconfig getifaddr en0
```

### Multi-device account linking

1. Log in on your primary device
2. Click **Devices** in the header
3. Click **Link New Device**
4. On the new device, either:
   - Scan the QR code with the phone's camera, or
   - Open the app and click **Link existing account to this device**, then paste the token
5. Enter a device name and tap **Link Device**

## Architecture

```
Browser ──► Nginx (:80)
              ├── /           → serves React SPA
              ├── /api/*      → proxies to backend (:9001)
              └── /ws         → proxies WebSocket to backend (:9001)

Backend (:9001) ──► PostgreSQL (:5432)
```

Authentication uses ECDSA P-256 keypairs stored in the browser's IndexedDB. No passwords are involved -- each device holds a private key and the server stores the corresponding public key.
