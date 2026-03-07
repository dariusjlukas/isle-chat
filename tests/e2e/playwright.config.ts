import { defineConfig } from "@playwright/test";

const FRONTEND_PORT = process.env.TEST_FRONTEND_PORT ?? "5199";
const WORKERS = parseInt(process.env.TEST_WORKERS ?? "8");
const PARALLEL = WORKERS > 1;

export default defineConfig({
  testDir: "./specs",
  fullyParallel: PARALLEL,
  retries: 0,
  workers: WORKERS,
  reporter: [["list"], ["html", { open: "never" }]],
  timeout: 30_000,
  use: {
    // In single-worker mode, use the single frontend. In multi-worker mode,
    // each spec overrides baseURL via the workerConfig fixture.
    baseURL: PARALLEL
      ? undefined
      : `http://localhost:${FRONTEND_PORT}`,
    trace: "retain-on-failure",
    screenshot: "only-on-failure",
    video: "retain-on-failure",
  },
  projects: [
    {
      name: "chromium",
      use: { browserName: "chromium" },
    },
  ],
});
