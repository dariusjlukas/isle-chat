/**
 * E2E tests for admin panel: settings, invites.
 */

import { test, expect } from "../fixtures.js";
import { resetDatabase } from "../helpers/db.js";
import {
  setupAdminUser,
  setupRegularUser,
  loginViaToken,
  type TestUser,
} from "../helpers/auth.js";

let admin: TestUser;

test.beforeEach(async ({ workerConfig }) => {
  resetDatabase(workerConfig.dbConfig);
  admin = await setupAdminUser(workerConfig.apiConfig);
});

/** Open the user avatar dropdown menu in the header. */
async function openAvatarMenu(page: import("@playwright/test").Page) {
  const avatarBtn = page.locator(
    "header .flex.items-center.justify-end button.rounded-full",
  );
  await avatarBtn.click();
}

/** Open Admin Panel via the avatar dropdown. */
async function clickAdminPanel(page: import("@playwright/test").Page) {
  await openAvatarMenu(page);
  await page.getByRole("menuitem", { name: "Admin Panel" }).click();
}

/** Open User Settings via the avatar dropdown. */
async function clickUserSettings(page: import("@playwright/test").Page) {
  await openAvatarMenu(page);
  await page.getByRole("menuitem", { name: "User Settings" }).click();
}

test.describe("Admin panel access", () => {
  test("admin can open admin panel", async ({ page }) => {
    await loginViaToken(page, admin.token);

    await clickAdminPanel(page);

    await expect(page.getByText("Admin Panel").first()).toBeVisible({
      timeout: 10_000,
    });

    await expect(
      page.getByRole("button", { name: "Server Settings" }),
    ).toBeVisible();
    await expect(
      page.getByRole("button", { name: "Invite Tokens" }),
    ).toBeVisible();
    await expect(
      page.getByRole("button", { name: "Account Recovery" }),
    ).toBeVisible();
    await expect(
      page.getByRole("button", { name: "Join Requests" }),
    ).toBeVisible();
  });

  test("regular user cannot see admin button", async ({
    page,
    workerConfig,
  }) => {
    const regular = await setupRegularUser(
      "regular",
      "Regular User",
      workerConfig.apiConfig,
    );
    await loginViaToken(page, regular.token);

    // Open avatar menu and verify no Admin Panel item
    await openAvatarMenu(page);
    await expect(
      page.getByRole("menuitem", { name: "Admin Panel" }),
    ).not.toBeVisible();
  });
});

test.describe("Server settings", () => {
  test("admin can view server settings", async ({ page }) => {
    await loginViaToken(page, admin.token);

    await clickAdminPanel(page);
    await expect(page.getByText("Admin Panel").first()).toBeVisible();

    await page.getByRole("button", { name: "Server Settings" }).click();

    await expect(page.getByText("Registration Mode")).toBeVisible({
      timeout: 5_000,
    });
  });
});

test.describe("Invite tokens", () => {
  test("admin can view invite tokens section", async ({ page }) => {
    await loginViaToken(page, admin.token);

    await clickAdminPanel(page);

    await page.getByRole("button", { name: "Invite Tokens" }).click();

    await expect(
      page.getByRole("button", { name: /generate|create/i }),
    ).toBeVisible({ timeout: 5_000 });
  });

  test("admin can generate and revoke an invite token", async ({ page }) => {
    await loginViaToken(page, admin.token);

    await clickAdminPanel(page);
    await page.getByRole("button", { name: "Invite Tokens" }).click();

    // Generate an invite
    await page.getByRole("button", { name: /generate/i }).click();

    // Wait for the invite to appear with Copy and Revoke buttons
    await expect(
      page.getByRole("button", { name: "Copy" }).first(),
    ).toBeVisible({ timeout: 5_000 });
    await expect(
      page.getByRole("button", { name: "Revoke" }).first(),
    ).toBeVisible();

    // Revoke the invite
    await page.getByRole("button", { name: "Revoke" }).first().click();

    // After revoking, the invite should be gone
    await expect(
      page.getByRole("button", { name: "Revoke" }),
    ).toHaveCount(0, { timeout: 5_000 });
  });
});

test.describe("User settings", () => {
  test("user can open settings modal", async ({ page }) => {
    await loginViaToken(page, admin.token);

    await clickUserSettings(page);

    await expect(page.getByText("Settings").first()).toBeVisible({
      timeout: 5_000,
    });
    await expect(
      page.getByRole("button", { name: "Profile" }),
    ).toBeVisible();
    await expect(
      page.getByRole("button", { name: "Appearance" }),
    ).toBeVisible();
  });
});
