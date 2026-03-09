/**
 * E2E tests for self-demotion: owners/admins can demote themselves via the UI.
 *
 * This is a regression test for a bug where the role dropdown was hidden for
 * the current user, preventing self-demotion even though the backend allowed it.
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
} from "../helpers/api.js";

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

const ADMIN_BTN = 0;

test.describe("Server-level self-demotion", () => {
  test("owner can see role dropdown for themselves in User Management", async ({
    page,
    workerConfig,
  }) => {
    // Create a second owner so demotion is allowed
    const regular = await setupRegularUser(
      "user2",
      "User Two",
      workerConfig.apiConfig,
    );
    await apiChangeUserRole(
      regular.userId,
      "owner",
      admin.token,
      workerConfig.apiConfig,
    );

    await loginViaToken(page, admin.token);

    // Open admin panel
    await clickHeaderButton(page, ADMIN_BTN);
    await expect(page.getByText("Admin Panel").first()).toBeVisible({
      timeout: 10_000,
    });

    // Expand User Management section
    await page.getByRole("button", { name: "User Management" }).click();

    // Wait for users to load - find our own username
    await expect(page.getByText("@admin").first()).toBeVisible({
      timeout: 5_000,
    });

    // The admin user's row should have a role Select (not just a text label)
    const adminRow = page
      .locator(".rounded-lg.bg-content1")
      .filter({ hasText: "@admin" });
    const roleSelect = adminRow.locator("select");
    await expect(roleSelect.first()).toBeAttached();
  });

  test("owner can demote themselves to admin via UI", async ({
    page,
    workerConfig,
  }) => {
    // Create a second owner so demotion is allowed
    const regular = await setupRegularUser(
      "user2",
      "User Two",
      workerConfig.apiConfig,
    );
    await apiChangeUserRole(
      regular.userId,
      "owner",
      admin.token,
      workerConfig.apiConfig,
    );

    await loginViaToken(page, admin.token);

    // Open admin panel
    await clickHeaderButton(page, ADMIN_BTN);
    await expect(page.getByText("Admin Panel").first()).toBeVisible({
      timeout: 10_000,
    });

    // Expand User Management section
    await page.getByRole("button", { name: "User Management" }).click();
    await expect(page.getByText("@admin").first()).toBeVisible({
      timeout: 5_000,
    });

    // Find the admin user's row and change the role via the hidden native <select>
    const adminRow = page
      .locator(".rounded-lg.bg-content1")
      .filter({ hasText: "@admin" });
    const nativeSelect = adminRow.locator("select");
    await nativeSelect.selectOption("admin", { force: true });

    // Verify the role changed via the API
    const users = await apiGetAdminUsers(
      admin.token,
      workerConfig.apiConfig,
    );
    const self = users.find((u) => u.username === "admin");
    expect(self?.role).toBe("admin");
  });
});
