/**
 * E2E tests for admin panel: settings, invites.
 */

import { test, expect, type Page } from "@playwright/test";
import { resetDatabase } from "../helpers/db.js";
import {
  setupAdminUser,
  setupRegularUser,
  loginViaToken,
  type TestUser,
} from "../helpers/auth.js";

let admin: TestUser;

test.beforeEach(async () => {
  resetDatabase();
  admin = await setupAdminUser();
});

/** Click the Nth button (0-indexed) in the header's right button group. */
async function clickHeaderButton(page: Page, index: number) {
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

    // Admin panel modal should open
    await expect(page.getByText("Admin Panel").first()).toBeVisible({
      timeout: 10_000,
    });

    // Accordion sections should be visible
    await expect(page.getByText("Server Settings")).toBeVisible();
    await expect(page.getByText("Invite Tokens")).toBeVisible();
    await expect(page.getByText("Account Recovery")).toBeVisible();
    await expect(page.getByText("Join Requests")).toBeVisible();
  });

  test("regular user cannot see admin button", async ({ page }) => {
    const regular = await setupRegularUser("regular", "Regular User");
    await loginViaToken(page, regular.token);

    // For regular users, the header right buttons are only [Settings, Logout]
    // There should be no shield icon / admin button - verify only 2 buttons
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

    // Expand Server Settings accordion
    await page.getByText("Server Settings").click();

    // Should show registration mode setting
    await expect(page.getByText("Registration Mode")).toBeVisible({
      timeout: 5_000,
    });
  });
});

test.describe("Invite tokens", () => {
  test("admin can view invite tokens section", async ({ page }) => {
    await loginViaToken(page, admin.token);

    await clickHeaderButton(page, ADMIN_BTN);

    // Expand Invite Tokens accordion
    await page.getByText("Invite Tokens").click();

    // Should show invite management UI
    await expect(
      page.getByRole("button", { name: /generate|create/i }),
    ).toBeVisible({ timeout: 5_000 });
  });
});

test.describe("User settings", () => {
  test("user can open settings modal", async ({ page }) => {
    await loginViaToken(page, admin.token);

    await clickHeaderButton(page, SETTINGS_BTN);

    // Settings modal should open with accordion sections
    await expect(page.getByText("Settings").first()).toBeVisible({
      timeout: 5_000,
    });
    await expect(page.getByText("Profile")).toBeVisible();
    await expect(page.getByText("Appearance")).toBeVisible();
  });
});
