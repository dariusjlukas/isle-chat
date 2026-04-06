/**
 * E2E tests for personal spaces: enabling/disabling, sidebar visibility, tools.
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

test.describe("Personal Spaces", () => {
  test("admin can enable personal spaces from settings", async ({ page }) => {
    await loginViaToken(page, admin.token);

    // Open Admin Panel -> Server Settings
    await clickAdminPanel(page);
    await expect(page.getByText("Admin Panel").first()).toBeVisible({
      timeout: 10_000,
    });
    await page.getByRole("button", { name: "Server Settings" }).click();

    // Wait for settings to load
    await expect(page.getByText("Registration Mode")).toBeVisible({
      timeout: 5_000,
    });

    // Scroll down and find the Personal Spaces toggle
    const personalSpacesLabel = page.getByText("Personal Spaces").first();
    await personalSpacesLabel.scrollIntoViewIfNeeded();

    // Find the switch next to "Personal Spaces" and toggle it on
    const personalSpacesSection = page.locator("div", {
      has: personalSpacesLabel,
    }).first();
    const toggle = personalSpacesSection.locator('[role="switch"]').first();
    await toggle.click();

    // Save settings
    await page.getByRole("button", { name: /save/i }).click();

    // Reload the page and navigate back to verify it persisted
    await page.reload();
    await page.getByText("EnclaveStation").first().waitFor({ timeout: 10_000 });

    await clickAdminPanel(page);
    await page.getByRole("button", { name: "Server Settings" }).click();
    await expect(page.getByText("Registration Mode")).toBeVisible({
      timeout: 5_000,
    });

    // Verify the toggle is still enabled
    const personalSpacesLabelReloaded = page
      .getByText("Personal Spaces")
      .first();
    await personalSpacesLabelReloaded.scrollIntoViewIfNeeded();
    const sectionReloaded = page.locator("div", {
      has: personalSpacesLabelReloaded,
    }).first();
    const toggleReloaded = sectionReloaded
      .locator('[role="switch"]')
      .first();
    await expect(toggleReloaded).toBeChecked();
  });

  test("personal space appears in sidebar when enabled", async ({
    page,
    workerConfig,
  }) => {
    const regular = await setupRegularUser(
      "regular",
      "Regular User",
      workerConfig.apiConfig,
    );

    // Enable personal spaces via API
    await apiEnablePersonalSpaces(admin.token, workerConfig.apiConfig);

    // Login as regular user
    await loginViaToken(page, regular.token);

    // Verify "My Space" tooltip/icon appears in sidebar
    await expect(
      page.locator('[data-slot="content"]', { hasText: "My Space" }),
    ).toBeHidden(); // tooltip content is hidden until hover

    // The personal space button with the house icon should be visible in the icon rail
    const personalSpaceBtn = page.locator(
      "aside button",
    ).filter({
      has: page.locator('svg[data-icon="house-user"]'),
    });
    await expect(personalSpaceBtn).toBeVisible({ timeout: 10_000 });
  });

  test("personal space not visible when disabled", async ({
    page,
    workerConfig,
  }) => {
    const regular = await setupRegularUser(
      "regular",
      "Regular User",
      workerConfig.apiConfig,
    );

    // Do NOT enable personal spaces
    await loginViaToken(page, regular.token);

    // Wait for app to load fully
    await page.getByText("EnclaveStation").first().waitFor({ timeout: 10_000 });

    // The house-user icon for personal space should NOT be visible
    const personalSpaceBtn = page.locator(
      "aside button",
    ).filter({
      has: page.locator('svg[data-icon="house-user"]'),
    });
    await expect(personalSpaceBtn).not.toBeVisible();
  });

  test("clicking personal space shows tools without channels", async ({
    page,
    workerConfig,
  }) => {
    const regular = await setupRegularUser(
      "regular",
      "Regular User",
      workerConfig.apiConfig,
    );

    // Enable personal spaces via API
    await apiEnablePersonalSpaces(admin.token, workerConfig.apiConfig);

    // Login as regular user
    await loginViaToken(page, regular.token);

    // Click the personal space button in the icon rail
    const personalSpaceBtn = page.locator(
      "aside button",
    ).filter({
      has: page.locator('svg[data-icon="house-user"]'),
    });
    await expect(personalSpaceBtn).toBeVisible({ timeout: 10_000 });
    await personalSpaceBtn.click();

    // The sidebar animates its width over 200ms. Wait for the CSS
    // transition to fully complete before inspecting the width.
    await page.waitForTimeout(350);

    // If the space was already auto-selected, clicking toggled collapse.
    // Click again to re-expand.
    const isCollapsed = await page.evaluate(
      () =>
        (document.querySelector("aside")?.offsetWidth ?? 0) <= 64,
    );
    if (isCollapsed) {
      await personalSpaceBtn.click();
    }

    // Wait for panel content to be interactable (CSS transition done)
    await page
      .locator("aside button", { hasText: "Files" })
      .click({ trial: true, timeout: 10_000 });

    // Verify tool buttons are visible (Files, Wiki, Tasks, Calendar by default)
    await expect(page.locator("aside button", { hasText: "Files" })).toBeVisible();
    await expect(page.locator("aside button", { hasText: "Wiki" })).toBeVisible();
    await expect(page.locator("aside button", { hasText: "Tasks" })).toBeVisible();
    await expect(page.locator("aside button", { hasText: "Calendar" })).toBeVisible();

    // Verify NO "Channels" heading is visible (personal spaces don't have channels)
    await expect(
      page.locator("aside", { hasText: "Channels" }).locator("h4", { hasText: "Channels" }),
    ).not.toBeVisible();
  });

  test("disabled tool not shown in personal space", async ({
    page,
    workerConfig,
  }) => {
    const regular = await setupRegularUser(
      "regular",
      "Regular User",
      workerConfig.apiConfig,
    );

    // Enable personal spaces but disable calendar tool via admin settings API
    const res = await fetch(`${workerConfig.apiConfig.apiBase}/api/admin/settings`, {
      method: "PUT",
      headers: {
        "Content-Type": "application/json",
        Authorization: `Bearer ${admin.token}`,
      },
      body: JSON.stringify({
        personal_spaces_enabled: true,
        personal_spaces_calendar_enabled: false,
      }),
    });
    if (!res.ok) throw new Error(`Failed to update settings: ${res.status}`);

    // Login as regular user
    await loginViaToken(page, regular.token);

    // Click the personal space button
    const personalSpaceBtn = page.locator(
      "aside button",
    ).filter({
      has: page.locator('svg[data-icon="house-user"]'),
    });
    await expect(personalSpaceBtn).toBeVisible({ timeout: 10_000 });
    await personalSpaceBtn.click();

    // The sidebar animates its width over 200ms. Wait for the CSS
    // transition to fully complete before inspecting the width.
    await page.waitForTimeout(350);

    // If the space was already auto-selected, clicking toggled collapse.
    // Click again to re-expand.
    const isCollapsed2 = await page.evaluate(
      () =>
        (document.querySelector("aside")?.offsetWidth ?? 0) <= 64,
    );
    if (isCollapsed2) {
      await personalSpaceBtn.click();
    }

    // Wait for panel content to be interactable (CSS transition done)
    await page
      .locator("aside button", { hasText: "Files" })
      .click({ trial: true, timeout: 10_000 });

    // Verify Calendar is NOT visible, but Files/Tasks/Wiki are
    await expect(page.locator("aside button", { hasText: "Files" })).toBeVisible();
    await expect(page.locator("aside button", { hasText: "Tasks" })).toBeVisible();
    await expect(page.locator("aside button", { hasText: "Wiki" })).toBeVisible();
    await expect(
      page.locator("aside button", { hasText: "Calendar" }),
    ).not.toBeVisible();
  });
});
