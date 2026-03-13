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

/** Open Admin Panel via the avatar dropdown. */
async function clickAdminPanel(page: import("@playwright/test").Page) {
  const avatarBtn = page.locator(
    "header .flex.items-center.justify-end button.rounded-full",
  );
  await avatarBtn.click();
  await page.getByRole("menuitem", { name: "Admin Panel" }).click();
}

/** Open admin panel → User Management. */
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
  await expect(page.getByText(`@${username}`).first()).toBeVisible({
    timeout: 5_000,
  });
  await page
    .locator(".cursor-pointer")
    .filter({ hasText: `@${username}` })
    .first()
    .click();
}

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
    await openUserManagement(page);
    await selectUser(page, "admin");

    // The user card should have a role dropdown (HeroUI Select trigger button)
    // that shows the current role
    const roleCell = page.locator("td").filter({ hasText: "Role" }).first();
    await expect(roleCell).toBeVisible({ timeout: 5_000 });

    // The role trigger button should exist (indicates an editable Select, not a plain label)
    const roleTrigger = page.getByRole("button", { name: /Owner/i });
    await expect(roleTrigger).toBeVisible();
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
    await openUserManagement(page);
    await selectUser(page, "admin");

    // Click the role trigger button to open the dropdown
    const roleTrigger = page.getByRole("button", { name: /Owner Role/i });
    await expect(roleTrigger).toBeVisible({ timeout: 5_000 });
    await roleTrigger.click();

    // Wait for the popover/dropdown to appear and click the Admin option
    // HeroUI renders the listbox in a portal; use a CSS selector for the option
    const adminOption = page.locator("li").filter({ hasText: /^Admin$/ });
    await adminOption.click({ timeout: 5_000 });

    // Verify the role changed via the API
    const users = await apiGetAdminUsers(
      admin.token,
      workerConfig.apiConfig,
    );
    const self = users.find((u) => u.username === "admin");
    expect(self?.role).toBe("admin");
  });
});
