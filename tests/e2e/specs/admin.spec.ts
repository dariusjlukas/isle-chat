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

/** Click the Nth button (0-indexed) in the header's right button group. */
async function clickHeaderButton(
  page: import("@playwright/test").Page,
  index: number,
) {
  const buttons = page.locator(
    "header .flex.items-center.justify-end button",
  );
  await buttons.nth(index).click();
}

// Header right buttons for admin: [0]=Admin, [1]=Settings, [2]=Logout
const ADMIN_BTN = 0;
const SETTINGS_BTN = 1;

test.describe("Admin panel access", () => {
  test("admin can open admin panel", async ({ page }) => {
    await loginViaToken(page, admin.token);

    await clickHeaderButton(page, ADMIN_BTN);

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

    const headerButtons = page.locator(
      "header .flex.items-center.justify-end button",
    );
    await expect(headerButtons).toHaveCount(2);
  });
});

test.describe("Server settings", () => {
  test("admin can view server settings", async ({ page }) => {
    await loginViaToken(page, admin.token);

    await clickHeaderButton(page, ADMIN_BTN);
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

    await clickHeaderButton(page, ADMIN_BTN);

    await page.getByRole("button", { name: "Invite Tokens" }).click();

    await expect(
      page.getByRole("button", { name: /generate|create/i }),
    ).toBeVisible({ timeout: 5_000 });
  });
});

test.describe("User settings", () => {
  test("user can open settings modal", async ({ page }) => {
    await loginViaToken(page, admin.token);

    await clickHeaderButton(page, SETTINGS_BTN);

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
