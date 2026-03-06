import { defineConfig } from "@playwright/test";

const BACKEND_PORT = process.env.TEST_BACKEND_PORT ?? "9099";
const FRONTEND_PORT = process.env.TEST_FRONTEND_PORT ?? "5199";

export default defineConfig({
  testDir: "./specs",
  fullyParallel: false, // tests modify shared server state
  retries: 0,
  workers: 1,
  reporter: [["list"], ["html", { open: "never" }]],
  timeout: 30_000,
  use: {
    baseURL: `http://localhost:${FRONTEND_PORT}`,
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
