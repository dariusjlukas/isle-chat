/**
 * Tests for WebSocket heartbeat and disconnect detection.
 *
 * Verifies that:
 * 1. The heartbeat runs continuously and last-pong timestamp updates regularly
 * 2. When the backend dies, the "Connection Lost" modal appears promptly
 */

import { test, expect } from "../fixtures.js";
import { resetDatabase } from "../helpers/db.js";
import { setupAdminUser, loginViaToken } from "../helpers/auth.js";
import {
  apiCreateSpace,
  apiEnableCalendarTool,
} from "../helpers/api.js";
import type { TestUser } from "../helpers/auth.js";

interface WsDebug {
  connectionState: string;
  hasConnected: boolean;
  lastHeartbeat: number | null;
  lastPingMs: number | null;
}

function getWsDebug(page: import("@playwright/test").Page): Promise<WsDebug> {
  return page.evaluate(
    () => (window as unknown as { __wsDebug: () => WsDebug }).__wsDebug(),
  );
}

let admin: TestUser;

test.beforeEach(async ({ workerConfig }) => {
  resetDatabase(workerConfig.dbConfig);
  admin = await setupAdminUser(workerConfig.apiConfig);
});

test("heartbeat keeps running and last-pong timestamp updates", async ({
  page,
}) => {
  test.setTimeout(45_000);
  await loginViaToken(page, admin.token);

  // Wait for WebSocket to connect and first heartbeat to complete
  await expect
    .poll(async () => (await getWsDebug(page)).connectionState, {
      timeout: 10_000,
    })
    .toBe("connected");

  // Wait for at least one pong to arrive
  await expect
    .poll(async () => (await getWsDebug(page)).lastHeartbeat, {
      timeout: 15_000,
    })
    .not.toBeNull();

  // Record the heartbeat timestamp
  const firstDebug = await getWsDebug(page);
  const firstPong = firstDebug.lastHeartbeat!;

  // Wait 15 seconds (3 heartbeat cycles) and verify the timestamp has advanced
  await page.waitForTimeout(15_000);

  const secondDebug = await getWsDebug(page);
  const secondPong = secondDebug.lastHeartbeat!;

  expect(secondPong).toBeGreaterThan(firstPong);
  // Should have advanced by roughly 15 seconds (±5s tolerance)
  expect(secondPong - firstPong).toBeGreaterThanOrEqual(10_000);
  expect(secondPong - firstPong).toBeLessThanOrEqual(25_000);
  // Connection should still be connected
  expect(secondDebug.connectionState).toBe("connected");
});

test("heartbeat still running after 60 seconds", async ({ page }) => {
  test.setTimeout(90_000);
  await loginViaToken(page, admin.token);

  // Wait for connection and first pong
  await expect
    .poll(async () => (await getWsDebug(page)).lastHeartbeat, {
      timeout: 15_000,
    })
    .not.toBeNull();

  // Wait 60 seconds — this is the key test: the heartbeat must not stop
  await page.waitForTimeout(60_000);

  const debug = await getWsDebug(page);
  const now = Date.now();
  expect(debug.connectionState).toBe("connected");
  expect(debug.lastHeartbeat).not.toBeNull();
  // Last pong should be within the last 15 seconds (interval + timeout budget)
  const age = now - debug.lastHeartbeat!;
  expect(age).toBeLessThan(15_000);
});

test("heartbeat survives navigation to tool views and back", async ({
  page,
  workerConfig,
}) => {
  test.setTimeout(60_000);

  // Set up a space with the calendar tool enabled
  const space = await apiCreateSpace(
    "Test Space",
    admin.token,
    undefined,
    workerConfig.apiConfig,
  );
  await apiEnableCalendarTool(
    space.id,
    admin.token,
    workerConfig.apiConfig,
  );

  await loginViaToken(page, admin.token);

  // Wait for connected + first pong
  await expect
    .poll(async () => (await getWsDebug(page)).connectionState, {
      timeout: 10_000,
    })
    .toBe("connected");
  await expect
    .poll(async () => (await getWsDebug(page)).lastHeartbeat, {
      timeout: 15_000,
    })
    .not.toBeNull();

  // Navigate to the space
  await page.getByText("Test Space").click();
  await page.waitForTimeout(1_000);

  // Navigate to the Calendar tool (unmounts ChatArea)
  await page.getByRole("button", { name: "Calendar" }).click();
  await page.waitForTimeout(1_000);

  // Record heartbeat timestamp while in Calendar view
  const beforeDebug = await getWsDebug(page);
  const beforePong = beforeDebug.lastHeartbeat!;
  expect(beforeDebug.connectionState).toBe("connected");

  // Wait 15 seconds while in Calendar view
  await page.waitForTimeout(15_000);

  // Heartbeat should still be updating while ChatArea is unmounted
  const afterDebug = await getWsDebug(page);
  expect(afterDebug.connectionState).toBe("connected");
  expect(afterDebug.lastHeartbeat).not.toBeNull();
  expect(afterDebug.lastHeartbeat!).toBeGreaterThan(beforePong);
  // Last pong should be recent, not stale from before navigation
  const age = Date.now() - afterDebug.lastHeartbeat!;
  expect(age).toBeLessThan(15_000);
});

test("connection lost modal appears when backend is killed", async ({
  page,
  workerConfig,
}) => {
  const pid = workerConfig.backendPid;
  // This test requires multi-worker mode where we control the backend process
  test.skip(!pid, "Requires multi-worker mode with per-worker backend");

  await loginViaToken(page, admin.token);

  // Wait for connected state
  await expect
    .poll(async () => (await getWsDebug(page)).connectionState, {
      timeout: 10_000,
    })
    .toBe("connected");

  // Wait for at least one heartbeat so hasConnected is true
  await expect
    .poll(async () => (await getWsDebug(page)).lastHeartbeat, {
      timeout: 15_000,
    })
    .not.toBeNull();

  // Kill the backend abruptly (SIGKILL — no graceful shutdown)
  process.kill(pid!, "SIGKILL");

  // The "Connection Lost" modal should appear within 20 seconds
  // (5s heartbeat interval + 10s pong timeout + margin)
  await expect(page.getByText("Connection Lost")).toBeVisible({
    timeout: 20_000,
  });

  // Restart the backend so other tests sharing this worker are not affected
  await workerConfig.restartBackend!();
});
