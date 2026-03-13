/**
 * E2E tests for banning users from the admin panel.
 */

import { test, expect } from "../fixtures.js";
import { resetDatabase } from "../helpers/db.js";
import {
  setupAdminUser,
  setupRegularUser,
  loginViaToken,
  type TestUser,
} from "../helpers/auth.js";
import {
  apiChangeUserRole,
  apiGetAdminUsers,
  apiBanUser,
  apiUnbanUser,
} from "../helpers/api.js";

let admin: TestUser;

test.beforeEach(async ({ workerConfig }) => {
  resetDatabase(workerConfig.dbConfig);
  admin = await setupAdminUser(workerConfig.apiConfig);
});

/** Open Admin Panel via the avatar dropdown. */
async function clickAdminPanel(page: import("@playwright/test").Page) {
  const avatarBtn = page.locator(
    "header .flex.items-center.justify-end button.rounded-full",
  );
  await avatarBtn.click();
  await page.getByRole("menuitem", { name: "Admin Panel" }).click();
}

/** Open admin panel → User Management, then click a user in the picker. */
async function openUserManagement(page: import("@playwright/test").Page) {
  await clickAdminPanel(page);
  await expect(page.getByText("Admin Panel").first()).toBeVisible({
    timeout: 10_000,
  });
  await page.getByRole("button", { name: "User Management" }).click();
}

/** Select a user by their username in the UserPicker. */
async function selectUser(
  page: import("@playwright/test").Page,
  username: string,
) {
  // Wait for users to load in the picker
  await expect(page.getByText(`@${username}`).first()).toBeVisible({
    timeout: 5_000,
  });
  // Click the user row in the picker list
  await page
    .locator(".cursor-pointer")
    .filter({ hasText: `@${username}` })
    .first()
    .click();
}

test.describe("Ban user via admin panel UI", () => {
  test("owner can ban a regular user", async ({ page, workerConfig }) => {
    const regular = await setupRegularUser(
      "banme",
      "Ban Me",
      workerConfig.apiConfig,
    );

    await loginViaToken(page, admin.token);
    await openUserManagement(page);
    await selectUser(page, "banme");

    // Verify user card shows with Active status and Ban button
    await expect(page.getByText("Active")).toBeVisible({ timeout: 5_000 });
    const banButton = page.getByRole("button", { name: "Ban User" });
    await expect(banButton).toBeVisible();

    // Click ban
    await banButton.click();

    // After banning, should show "Banned" status and "Unban User" button
    await expect(page.getByText("Banned").first()).toBeVisible({
      timeout: 5_000,
    });
    await expect(
      page.getByRole("button", { name: "Unban User" }),
    ).toBeVisible();

    // Verify via API
    const users = await apiGetAdminUsers(
      admin.token,
      workerConfig.apiConfig,
    );
    const banned = users.find((u) => u.username === "banme");
    expect(banned?.is_banned).toBe(true);
  });

  test("owner can unban a user", async ({ page, workerConfig }) => {
    const regular = await setupRegularUser(
      "unbanme",
      "Unban Me",
      workerConfig.apiConfig,
    );
    // Ban via API first
    await apiBanUser(regular.userId, admin.token, workerConfig.apiConfig);

    await loginViaToken(page, admin.token);
    await openUserManagement(page);
    await selectUser(page, "unbanme");

    // Should show Banned status and Unban button
    await expect(page.getByText("Banned").first()).toBeVisible({
      timeout: 5_000,
    });
    const unbanButton = page.getByRole("button", { name: "Unban User" });
    await expect(unbanButton).toBeVisible();

    // Click unban
    await unbanButton.click();

    // After unbanning, should show Active status and Ban button
    await expect(page.getByText("Active")).toBeVisible({ timeout: 5_000 });
    await expect(
      page.getByRole("button", { name: "Ban User" }),
    ).toBeVisible();

    // Verify via API
    const users = await apiGetAdminUsers(
      admin.token,
      workerConfig.apiConfig,
    );
    const user = users.find((u) => u.username === "unbanme");
    expect(user?.is_banned).toBe(false);
  });

  test("ban button is not shown for users of equal or higher rank", async ({
    page,
    workerConfig,
  }) => {
    // Create a second owner
    const otherOwner = await setupRegularUser(
      "owner2",
      "Owner Two",
      workerConfig.apiConfig,
    );
    await apiChangeUserRole(
      otherOwner.userId,
      "owner",
      admin.token,
      workerConfig.apiConfig,
    );

    await loginViaToken(page, admin.token);
    await openUserManagement(page);
    await selectUser(page, "owner2");

    // Should NOT show ban button for another owner
    await expect(page.getByText("@owner2").first()).toBeVisible({ timeout: 5_000 });
    await expect(
      page.getByRole("button", { name: "Ban User" }),
    ).not.toBeVisible();
  });

  test("admin cannot see ban button for another admin", async ({
    page,
    workerConfig,
  }) => {
    // Create two users, promote both to admin
    const admin2 = await setupRegularUser(
      "admin2",
      "Admin Two",
      workerConfig.apiConfig,
    );
    const admin3 = await setupRegularUser(
      "admin3",
      "Admin Three",
      workerConfig.apiConfig,
    );
    await apiChangeUserRole(
      admin2.userId,
      "admin",
      admin.token,
      workerConfig.apiConfig,
    );
    await apiChangeUserRole(
      admin3.userId,
      "admin",
      admin.token,
      workerConfig.apiConfig,
    );

    // Login as admin2
    await loginViaToken(page, admin2.token);
    await openUserManagement(page);
    await selectUser(page, "admin3");

    // Should NOT show ban button for equal-rank admin
    await expect(page.getByText("@admin3").first()).toBeVisible({ timeout: 5_000 });
    await expect(
      page.getByRole("button", { name: "Ban User" }),
    ).not.toBeVisible();
  });

  test("admin can ban a regular user", async ({ page, workerConfig }) => {
    const adminUser = await setupRegularUser(
      "myadmin",
      "My Admin",
      workerConfig.apiConfig,
    );
    const regular = await setupRegularUser(
      "regular",
      "Regular",
      workerConfig.apiConfig,
    );
    await apiChangeUserRole(
      adminUser.userId,
      "admin",
      admin.token,
      workerConfig.apiConfig,
    );

    // Login as the admin (not owner)
    await loginViaToken(page, adminUser.token);
    await openUserManagement(page);
    await selectUser(page, "regular");

    // Should show ban button for lower-rank user
    const banButton = page.getByRole("button", { name: "Ban User" });
    await expect(banButton).toBeVisible({ timeout: 5_000 });

    await banButton.click();

    // After banning, should show banned status
    await expect(page.getByText("Banned").first()).toBeVisible({
      timeout: 5_000,
    });
  });
});

test.describe("Banned user session", () => {
  test("banned user is logged out when session is invalidated", async ({
    page,
    workerConfig,
  }) => {
    const regular = await setupRegularUser(
      "victim",
      "Victim User",
      workerConfig.apiConfig,
    );

    // Login as the regular user
    await loginViaToken(page, regular.token);
    await expect(page.getByText("EnclaveStation").first()).toBeVisible({
      timeout: 10_000,
    });

    // Ban the user via API (simulating admin action from another session)
    await apiBanUser(regular.userId, admin.token, workerConfig.apiConfig);

    // Reload the page — the banned user's token should be invalid
    await page.reload();

    // Should be redirected to the login/register page
    await expect(
      page.getByText("Sign in to continue"),
    ).toBeVisible({ timeout: 10_000 });
  });

  test("banned user shows as banned in admin user list", async ({
    page,
    workerConfig,
  }) => {
    const regular = await setupRegularUser(
      "listed",
      "Listed User",
      workerConfig.apiConfig,
    );
    await apiBanUser(regular.userId, admin.token, workerConfig.apiConfig);

    await loginViaToken(page, admin.token);
    await openUserManagement(page);
    await selectUser(page, "listed");

    // Should show "Banned" label in the user card
    await expect(page.getByText("Banned").first()).toBeVisible({
      timeout: 5_000,
    });
  });
});
