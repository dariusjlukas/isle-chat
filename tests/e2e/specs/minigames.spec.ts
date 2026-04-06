/**
 * E2E tests for the Minigames personal space tool.
 * Verifies the tool appears, can be launched, and respects admin settings.
 */

import { test, expect } from "../fixtures.js";
import { resetDatabase } from "../helpers/db.js";
import {
  setupAdminUser,
  setupRegularUser,
  loginViaToken,
  type TestUser,
} from "../helpers/auth.js";
import { apiEnablePersonalSpaces } from "../helpers/api.js";

let admin: TestUser;

test.beforeEach(async ({ workerConfig }) => {
  resetDatabase(workerConfig.dbConfig);
  admin = await setupAdminUser(workerConfig.apiConfig);
});

/** Click the personal space button in the icon rail. */
async function openPersonalSpace(page: import("@playwright/test").Page) {
  const personalSpaceBtn = page
    .locator("aside button")
    .filter({ has: page.locator('svg[data-icon="house-user"]') });
  await expect(personalSpaceBtn).toBeVisible({ timeout: 10_000 });
  await personalSpaceBtn.click();

  // The sidebar animates its width over 200ms. Wait for the CSS
  // transition to fully complete before inspecting the width.
  await page.waitForTimeout(350);

  // If the space was already auto-selected, clicking the icon toggled the
  // side-panel collapse. Click again to re-expand.
  const isCollapsed = await page.evaluate(
    () => (document.querySelector("aside")?.offsetWidth ?? 0) <= 64,
  );
  if (isCollapsed) {
    await personalSpaceBtn.click();
  }

  // Wait for the panel to finish its CSS transition and be interactable.
  await page
    .locator("aside button", { hasText: "Files" })
    .click({ trial: true, timeout: 10_000 });
}

test.describe("Minigames Tool", () => {
  test("minigames button appears in personal space by default", async ({
    page,
    workerConfig,
  }) => {
    const regular = await setupRegularUser(
      "regular",
      "Regular User",
      workerConfig.apiConfig,
    );
    await apiEnablePersonalSpaces(admin.token, workerConfig.apiConfig);
    await loginViaToken(page, regular.token);

    await openPersonalSpace(page);

    // Minigames button should be visible alongside the other tools
    await page
      .locator("aside button", { hasText: "Minigames" })
      .click({ trial: true, timeout: 5_000 });
  });

  test("clicking minigames shows the game selection view", async ({
    page,
    workerConfig,
  }) => {
    const regular = await setupRegularUser(
      "regular",
      "Regular User",
      workerConfig.apiConfig,
    );
    await apiEnablePersonalSpaces(admin.token, workerConfig.apiConfig);
    await loginViaToken(page, regular.token);

    await openPersonalSpace(page);

    // Click the Minigames button
    await page.locator("aside button", { hasText: "Minigames" }).click();

    // The game selection view should be visible with both game options
    await expect(page.getByText("Choose a game to play")).toBeVisible({
      timeout: 5_000,
    });
    await expect(page.getByText("Route PCB")).toBeVisible();
    await expect(page.getByText("Rubik's Cube")).toBeVisible();
  });

  test("can launch PCB Router game", async ({ page, workerConfig }) => {
    const regular = await setupRegularUser(
      "regular",
      "Regular User",
      workerConfig.apiConfig,
    );
    await apiEnablePersonalSpaces(admin.token, workerConfig.apiConfig);
    await loginViaToken(page, regular.token);

    await openPersonalSpace(page);
    await page.locator("aside button", { hasText: "Minigames" }).click();
    await expect(page.getByText("Choose a game to play")).toBeVisible({
      timeout: 5_000,
    });

    // Click Route PCB
    await page.getByText("Route PCB").click();

    // PCB game UI should appear (difficulty selector or game board)
    await expect(page.getByText("Easy")).toBeVisible({ timeout: 5_000 });

    // Back button should return to selection
    await page.getByText("Back to game selection").click();
    await expect(page.getByText("Choose a game to play")).toBeVisible();
  });

  test("can launch Rubik's Cube game", async ({ page, workerConfig }) => {
    const regular = await setupRegularUser(
      "regular",
      "Regular User",
      workerConfig.apiConfig,
    );
    await apiEnablePersonalSpaces(admin.token, workerConfig.apiConfig);
    await loginViaToken(page, regular.token);

    await openPersonalSpace(page);
    await page.locator("aside button", { hasText: "Minigames" }).click();
    await expect(page.getByText("Choose a game to play")).toBeVisible({
      timeout: 5_000,
    });

    // Click Rubik's Cube
    await page.getByText("Rubik's Cube").click();

    // Rubik's Cube UI should appear (difficulty selector or canvas)
    await expect(page.getByText("Easy")).toBeVisible({ timeout: 5_000 });

    // Back button should return to selection
    await page.getByText("Back to game selection").click();
    await expect(page.getByText("Choose a game to play")).toBeVisible();
  });

  test("minigames hidden when admin disables the tool", async ({
    page,
    workerConfig,
  }) => {
    const regular = await setupRegularUser(
      "regular",
      "Regular User",
      workerConfig.apiConfig,
    );

    // Enable personal spaces but disable minigames
    const res = await fetch(
      `${workerConfig.apiConfig.apiBase}/api/admin/settings`,
      {
        method: "PUT",
        headers: {
          "Content-Type": "application/json",
          Authorization: `Bearer ${admin.token}`,
        },
        body: JSON.stringify({
          personal_spaces_enabled: true,
          personal_spaces_minigames_enabled: false,
        }),
      },
    );
    if (!res.ok) throw new Error(`Failed to update settings: ${res.status}`);

    await loginViaToken(page, regular.token);
    await openPersonalSpace(page);

    // Minigames should NOT be visible, but other tools should be
    await expect(
      page.locator("aside button", { hasText: "Files" }),
    ).toBeVisible();
    await expect(
      page.locator("aside button", { hasText: "Minigames" }),
    ).not.toBeVisible();
  });
});
