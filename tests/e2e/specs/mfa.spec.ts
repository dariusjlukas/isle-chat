/**
 * E2E tests for multi-factor authentication and PKI PIN flows.
 */

import { test, expect } from "../fixtures.js";
import { resetDatabase } from "../helpers/db.js";
import {
  setupAdminUser,
  setupPasswordUser,
  loginViaToken,
  type TestUser,
  type PasswordTestUser,
} from "../helpers/auth.js";
import {
  apiEnablePasswordAuth,
  apiSetMfaRequired,
  apiPasswordLogin,
  apiVerifyMfa,
} from "../helpers/api.js";
import { generateTotpCode } from "../helpers/totp.js";

let admin: TestUser;

test.beforeEach(async ({ workerConfig }) => {
  resetDatabase(workerConfig.dbConfig);
  admin = await setupAdminUser(workerConfig.apiConfig);
  await apiEnablePasswordAuth(admin.token, workerConfig.apiConfig);
});

test.describe("Password login with MFA (TOTP)", () => {
  test("user with TOTP sees MFA screen after password login", async ({
    page,
    workerConfig,
  }) => {
    // Set up a password user with TOTP enabled
    const user = await setupPasswordUser(
      "mfauser",
      "TestPass123",
      { enableTotp: true },
      workerConfig.apiConfig,
    );

    await page.goto("/");

    // Fill in password login
    await page.getByLabel("Username").fill("mfauser");
    await page.getByLabel("Password").fill("TestPass123");
    await page.getByRole("button", { name: /sign in with password/i }).click();

    // Should see MFA screen
    await expect(
      page.getByText("Two-Factor Authentication"),
    ).toBeVisible({ timeout: 10_000 });
    await expect(
      page.getByText("Enter the 6-digit code"),
    ).toBeVisible();

    // Enter valid TOTP code — auto-submits on 6th digit
    const code = generateTotpCode(user.totpSecret!);
    await page.getByLabel("Verification Code").fill(code);

    // Should be logged in
    await expect(page.getByText("Isle Chat").first()).toBeVisible({
      timeout: 10_000,
    });
  });

  test("wrong TOTP code shows error", async ({ page, workerConfig }) => {
    await setupPasswordUser(
      "mfauser2",
      "TestPass123",
      { enableTotp: true },
      workerConfig.apiConfig,
    );

    await page.goto("/");
    await page.getByLabel("Username").fill("mfauser2");
    await page.getByLabel("Password").fill("TestPass123");
    await page.getByRole("button", { name: /sign in with password/i }).click();

    await expect(
      page.getByText("Two-Factor Authentication"),
    ).toBeVisible({ timeout: 10_000 });

    // Enter wrong code — auto-submits on 6th digit
    await page.getByLabel("Verification Code").fill("000000");

    // Should show error
    await expect(
      page.getByText(/invalid|incorrect/i),
    ).toBeVisible({ timeout: 5_000 });
  });

  test("back button returns to login form from MFA screen", async ({
    page,
    workerConfig,
  }) => {
    await setupPasswordUser(
      "mfauser3",
      "TestPass123",
      { enableTotp: true },
      workerConfig.apiConfig,
    );

    await page.goto("/");
    await page.getByLabel("Username").fill("mfauser3");
    await page.getByLabel("Password").fill("TestPass123");
    await page.getByRole("button", { name: /sign in with password/i }).click();

    await expect(
      page.getByText("Two-Factor Authentication"),
    ).toBeVisible({ timeout: 10_000 });

    await page.getByRole("button", { name: "Back to sign in" }).click();

    // Should be back on login form
    await expect(
      page.getByRole("button", { name: /sign in with password/i }),
    ).toBeVisible({ timeout: 5_000 });
  });
});

test.describe("Admin MFA requirement", () => {
  test("user without TOTP is forced to set it up at login", async ({
    page,
    workerConfig,
  }) => {
    // Require MFA for passwords
    await apiSetMfaRequired(
      { mfa_required_password: true },
      admin.token,
      workerConfig.apiConfig,
    );

    // Create password user without TOTP
    await setupPasswordUser(
      "nomfa",
      "TestPass123",
      { enableTotp: false },
      workerConfig.apiConfig,
    );

    await page.goto("/");
    await page.getByLabel("Username").fill("nomfa");
    await page.getByLabel("Password").fill("TestPass123");
    await page.getByRole("button", { name: /sign in with password/i }).click();

    // Should see the forced TOTP setup screen
    await expect(
      page.getByText("Set Up Two-Factor Authentication"),
    ).toBeVisible({ timeout: 10_000 });

    // Should show QR code and manual key
    await expect(
      page.getByText(/scan.*qr|authenticator/i),
    ).toBeVisible({ timeout: 5_000 });

    // Back button should return to login
    await page.getByRole("button", { name: "Back to sign in" }).click();
    await expect(
      page.getByRole("button", { name: /sign in with password/i }),
    ).toBeVisible({ timeout: 5_000 });
  });
});

test.describe("TOTP setup in user settings", () => {
  test("user can enable TOTP from settings", async ({
    page,
    workerConfig,
  }) => {
    // Create a password user without TOTP
    const user = await setupPasswordUser(
      "settingsuser",
      "TestPass123",
      { enableTotp: false },
      workerConfig.apiConfig,
    );
    await loginViaToken(page, user.token);

    // Open settings via avatar dropdown
    const avatarBtn = page.locator(
      "header .flex.items-center.justify-end button.rounded-full",
    );
    await avatarBtn.click();
    await page.getByRole("menuitem", { name: "User Settings" }).click();

    await expect(page.getByText("Settings").first()).toBeVisible({
      timeout: 5_000,
    });

    // Navigate to Two-Factor Auth section
    await page.getByRole("button", { name: "Two-Factor Auth" }).click();

    // Should show the TOTP setup prompt
    await expect(
      page.getByRole("button", { name: /set up two-factor/i }),
    ).toBeVisible({ timeout: 5_000 });

    // Click to enable
    await page
      .getByRole("button", { name: /set up two-factor/i })
      .click();

    // Should show QR code / setup form
    await expect(
      page.getByText(/scan.*qr|authenticator/i),
    ).toBeVisible({ timeout: 10_000 });
  });
});

test.describe("MFA admin settings UI", () => {
  test("admin can toggle MFA requirements in server settings", async ({
    page,
  }) => {
    await loginViaToken(page, admin.token);

    // Open admin panel via avatar dropdown
    const avatarBtn = page.locator(
      "header .flex.items-center.justify-end button.rounded-full",
    );
    await avatarBtn.click();
    await page.getByRole("menuitem", { name: "Admin Panel" }).click();

    await expect(page.getByText("Admin Panel").first()).toBeVisible({
      timeout: 10_000,
    });

    await page.getByRole("button", { name: "Server Settings" }).click();

    // Wait for settings to load
    await expect(page.getByText("Registration Mode")).toBeVisible({
      timeout: 5_000,
    });

    // Scroll down to find MFA section
    await expect(
      page.getByText("Multi-Factor Authentication"),
    ).toBeVisible({ timeout: 5_000 });

    // Should see toggles for password and PKI MFA
    await expect(page.getByText("Require MFA for password login")).toBeVisible();
    await expect(page.getByText("Require MFA for browser key login")).toBeVisible();
  });
});
