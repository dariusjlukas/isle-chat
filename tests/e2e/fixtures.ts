/**
 * Playwright worker-scoped fixtures for parallel test execution.
 *
 * Each worker gets its own isolated:
 *   - PostgreSQL database (in the shared container)
 *   - Backend server (on a unique port)
 *   - Vite dev server (on a unique port, proxying to its backend)
 *
 * This allows spec files to run in parallel without interfering with each other.
 *
 * When TEST_WORKERS=1 (default), the fixture is not used and tests run against
 * the shared infrastructure started by run-tests.sh (backward compatible).
 */

import { test as base } from "@playwright/test";
import { execSync, spawn, type ChildProcess } from "child_process";
import type { ApiConfig } from "./helpers/api.js";
import type { DbConfig } from "./helpers/db.js";

const PG_USER = process.env.POSTGRES_USER ?? "chatapp_test";
const PG_PASS = process.env.POSTGRES_PASSWORD ?? "testpassword";
const PG_CONTAINER =
  process.env.TEST_PG_CONTAINER ?? "chatapp-test-postgres";
const PG_PORT = process.env.POSTGRES_PORT ?? "5433";
const BASE_BACKEND_PORT = parseInt(
  process.env.TEST_BACKEND_PORT_BASE ?? "9100",
);
const BASE_FRONTEND_PORT = parseInt(
  process.env.TEST_FRONTEND_PORT_BASE ?? "5200",
);
const BUILD_DIR = process.env.TEST_BUILD_DIR ?? "";
const FRONTEND_DIR = process.env.TEST_FRONTEND_DIR ?? "";
const WORKERS = parseInt(process.env.TEST_WORKERS ?? "8");

export interface WorkerConfig {
  apiConfig: ApiConfig;
  dbConfig: DbConfig;
  frontendUrl: string;
}

function waitForUrl(url: string, timeoutMs = 30_000): Promise<boolean> {
  const start = Date.now();
  return new Promise((resolve) => {
    const check = () => {
      fetch(url)
        .then((r) => {
          if (r.ok || r.status < 500) resolve(true);
          else if (Date.now() - start > timeoutMs) resolve(false);
          else setTimeout(check, 500);
        })
        .catch(() => {
          if (Date.now() - start > timeoutMs) resolve(false);
          else setTimeout(check, 500);
        });
    };
    check();
  });
}

export const test = base.extend<
  object,
  { workerConfig: WorkerConfig }
>({
  // Override baseURL per-worker so page.goto("/") goes to the right Vite server
  baseURL: [
    async ({ workerConfig }, use) => {
      await use(workerConfig.frontendUrl);
    },
    { scope: "test" },
  ],

  workerConfig: [
    async ({}, use, workerInfo) => {
      if (WORKERS <= 1) {
        // Single-worker mode: use shared infrastructure from run-tests.sh
        const backendPort = process.env.TEST_BACKEND_PORT ?? "9098";
        const frontendPort = process.env.TEST_FRONTEND_PORT ?? "5199";
        const dbName = process.env.POSTGRES_DB ?? "chatapp_test";
        const config: WorkerConfig = {
          apiConfig: {
            apiBase: `http://localhost:${backendPort}`,
            pgUser: PG_USER,
            pgDb: dbName,
            pgContainer: PG_CONTAINER,
          },
          dbConfig: {
            pgUser: PG_USER,
            pgDb: dbName,
            pgContainer: PG_CONTAINER,
          },
          frontendUrl: `http://localhost:${frontendPort}`,
        };
        await use(config);
        return;
      }

      // Multi-worker mode: spin up per-worker infrastructure
      const idx = workerInfo.workerIndex;
      const dbName = `chatapp_test_w${idx}`;
      const backendPort = BASE_BACKEND_PORT + idx;
      const frontendPort = BASE_FRONTEND_PORT + idx;

      // Create the worker's database
      execSync(
        `docker exec ${PG_CONTAINER} psql -U "${PG_USER}" -c "DROP DATABASE IF EXISTS ${dbName}"`,
        { stdio: "pipe" },
      );
      execSync(
        `docker exec ${PG_CONTAINER} psql -U "${PG_USER}" -c "CREATE DATABASE ${dbName} OWNER ${PG_USER}"`,
        { stdio: "pipe" },
      );

      // Start backend server
      const uploadDir = execSync("mktemp -d", { encoding: "utf-8" }).trim();
      const backendProc: ChildProcess = spawn(
        `${BUILD_DIR}/chat-server`,
        [],
        {
          env: {
            ...process.env,
            BACKEND_PORT: String(backendPort),
            POSTGRES_HOST: "localhost",
            POSTGRES_PORT: PG_PORT,
            POSTGRES_USER: PG_USER,
            POSTGRES_PASSWORD: PG_PASS,
            POSTGRES_DB: dbName,
            UPLOAD_DIR: uploadDir,
          },
          stdio: "pipe",
        },
      );

      const backendReady = await waitForUrl(
        `http://127.0.0.1:${backendPort}/api/health`,
        15_000,
      );
      if (!backendReady) {
        backendProc.kill();
        throw new Error(
          `Worker ${idx}: backend failed to start on port ${backendPort}`,
        );
      }

      // Start Vite dev server proxying to this worker's backend
      const viteProc: ChildProcess = spawn(
        "npx",
        ["vite", "--port", String(frontendPort), "--strictPort"],
        {
          cwd: FRONTEND_DIR,
          env: {
            ...process.env,
            VITE_BACKEND_PORT: String(backendPort),
          },
          stdio: "pipe",
        },
      );

      const viteReady = await waitForUrl(
        `http://localhost:${frontendPort}/`,
        30_000,
      );
      if (!viteReady) {
        viteProc.kill();
        backendProc.kill();
        throw new Error(
          `Worker ${idx}: Vite failed to start on port ${frontendPort}`,
        );
      }

      const config: WorkerConfig = {
        apiConfig: {
          apiBase: `http://localhost:${backendPort}`,
          pgUser: PG_USER,
          pgDb: dbName,
          pgContainer: PG_CONTAINER,
        },
        dbConfig: {
          pgUser: PG_USER,
          pgDb: dbName,
          pgContainer: PG_CONTAINER,
        },
        frontendUrl: `http://localhost:${frontendPort}`,
      };

      await use(config);

      // Teardown
      viteProc.kill();
      backendProc.kill();
      try {
        execSync(`rm -rf "${uploadDir}"`, { stdio: "pipe" });
      } catch {}
      try {
        execSync(
          `docker exec ${PG_CONTAINER} psql -U "${PG_USER}" -c "DROP DATABASE IF EXISTS ${dbName}"`,
          { stdio: "pipe" },
        );
      } catch {}
    },
    { scope: "worker" },
  ],
});

export { expect } from "@playwright/test";
