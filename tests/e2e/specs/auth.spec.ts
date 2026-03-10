/**
 * E2E tests for authentication flows: registration, login, logout, recovery.
 */

import { test, expect } from "../fixtures.js";
import { resetDatabase } from "../helpers/db.js";
import {
  setupAdminUser,
  setupRegularUser,
  loginViaToken,
  type TestUser,
} from "../helpers/auth.js";

test.beforeEach(({ workerConfig }) => {
  resetDatabase(workerConfig.dbConfig);
});

test.describe("Registration via UI", () => {
  test("first user can register with browser key", async ({ page }) => {
    await page.goto("/");
    // Should show login page
    await expect(
      page.getByRole("button", { name: /sign in with/i }),
    ).toBeVisible();

    // Navigate to register
    await page.getByText("Create an account").click();

    // Fill in registration form
    await page.getByPlaceholder("johndoe").fill("firstuser");
    await page.getByPlaceholder("John Doe").fill("First User");

    // Switch to Browser Key tab, fill PIN, and click register
    await page.getByText("Browser Key").click();
    await page.getByLabel("Browser Key PIN").fill("testpin1234");
    await page.getByLabel("Confirm PIN").fill("testpin1234");
    await page
      .getByRole("button", { name: /create account with browser key/i })
      .click();

    // Recovery keys modal should appear
    await expect(
      page.getByText("Recovery Keys", { exact: true }),
    ).toBeVisible({ timeout: 10_000 });

    // Check the "I have saved" checkbox
    await page
      .getByRole("checkbox", { name: /I have saved these recovery keys/i })
      .check({ force: true });

    // Click Continue
    await page.getByRole("button", { name: "Continue" }).click();

    // Should be logged in - look for the app content
    await expect(page.getByText("Isle Chat").first()).toBeVisible({
      timeout: 10_000,
    });

    // First user triggers Setup Wizard - complete it
    const setupWizard = page.getByText("Welcome! Server Setup");
    if (await setupWizard.isVisible().catch(() => false)) {
      await page.getByRole("button", { name: "Complete Setup" }).click();
      await setupWizard.waitFor({ state: "hidden", timeout: 5_000 });
    }
  });

  test("second user can register when registration is open", async ({
    page,
    workerConfig,
  }) => {
    // Set up admin and open registration via API
    await setupAdminUser(workerConfig.apiConfig);

    await page.goto("/");
    await page.getByText("Create an account").click();

    await page.getByPlaceholder("johndoe").fill("seconduser");
    await page.getByPlaceholder("John Doe").fill("Second User");
    await page.getByText("Browser Key").click();
    await page.getByLabel("Browser Key PIN").fill("testpin1234");
    await page.getByLabel("Confirm PIN").fill("testpin1234");
    await page
      .getByRole("button", { name: /create account with browser key/i })
      .click();

    await expect(
      page.getByText("Recovery Keys", { exact: true }),
    ).toBeVisible({ timeout: 10_000 });
    await page
      .getByRole("checkbox", { name: /I have saved these recovery keys/i })
      .check({ force: true });
    await page.getByRole("button", { name: "Continue" }).click();

    await expect(page.getByText("Isle Chat").first()).toBeVisible({
      timeout: 10_000,
    });
  });
});

test.describe("Login via token injection", () => {
  test("user can access app with valid session token", async ({
    page,
    workerConfig,
  }) => {
    const admin = await setupAdminUser(workerConfig.apiConfig);
    await loginViaToken(page, admin.token);

    // Should see the app with "Welcome to Isle Chat" or sidebar
    await expect(page.getByText("Isle Chat").first()).toBeVisible();
  });
});

test.describe("Logout", () => {
  test("user can log out via header button", async ({
    page,
    workerConfig,
  }) => {
    const admin = await setupAdminUser(workerConfig.apiConfig);
    await loginViaToken(page, admin.token);

    // Click the logout button - last button in the header right section
    const headerButtons = page.locator(
      "header .flex.items-center.justify-end button",
    );
    await headerButtons.last().click();

    // Should return to login page
    await expect(
      page.getByRole("button", { name: /sign in with/i }),
    ).toBeVisible({ timeout: 10_000 });
  });
});

test.describe("Recovery key login", () => {
  test("user can log in with a recovery key", async ({
    page,
    workerConfig,
  }) => {
    const admin = await setupAdminUser(workerConfig.apiConfig);
    const user = await setupRegularUser(
      "recoverable",
      "Recoverable User",
      workerConfig.apiConfig,
    );

    await page.goto("/");
    // Click "Use a recovery key"
    await page.getByText("Use a recovery key").click();

    // Fill in the recovery key
    await page
      .getByPlaceholder("XXXX-XXXX-XXXX-XXXX-XXXX")
      .fill(user.recoveryKeys[0]);
    await page.getByRole("button", { name: "Recover Account" }).click();

    // Should be logged in
    await expect(page.getByText("Isle Chat").first()).toBeVisible({
      timeout: 10_000,
    });
  });
});
