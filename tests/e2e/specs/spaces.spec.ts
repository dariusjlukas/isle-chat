/**
 * E2E tests for spaces: creation, navigation, channel management within spaces.
 */

import { test, expect } from "@playwright/test";
import { resetDatabase } from "../helpers/db.js";
import {
  setupAdminUser,
  loginViaToken,
  type TestUser,
} from "../helpers/auth.js";
import { apiCreateSpace } from "../helpers/api.js";

let admin: TestUser;

test.beforeEach(async () => {
  resetDatabase();
  admin = await setupAdminUser();
});

test.describe("Space creation via UI", () => {
  test("admin can create a space", async ({ page }) => {
    await loginViaToken(page, admin.token);

    // Click the dashed-border "+" button in the icon rail (left bar) to browse spaces
    await page.locator("button.border-dashed").click();

    // SpaceBrowser modal should open - click "Create New Space"
    await page.getByRole("button", { name: /create new space/i }).click();

    // Fill in the create space form
    await page.getByPlaceholder("e.g. Engineering").fill("Engineering");
    await page
      .getByPlaceholder("What's this space for?")
      .fill("Engineering team discussions");

    // Submit
    await page.getByRole("button", { name: "Create Space" }).click();

    // Space should be created and visible in the sidebar
    await expect(
      page.getByRole("heading", { name: "Engineering" })
    ).toBeVisible({ timeout: 10_000 });
  });
});

test.describe("Space navigation", () => {
  test("user can switch between spaces", async ({ page }) => {
    await apiCreateSpace("Space One", admin.token);
    await apiCreateSpace("Space Two", admin.token);

    await loginViaToken(page, admin.token);

    // First space should be auto-selected and visible
    await expect(page.getByText("Space One")).toBeVisible({ timeout: 10_000 });
  });
});

test.describe("Channel creation in space", () => {
  test("admin can create a channel within a space", async ({ page }) => {
    await apiCreateSpace("Dev Team", admin.token);

    await loginViaToken(page, admin.token);

    // Wait for the space to load in sidebar
    await expect(page.getByText("Dev Team")).toBeVisible({ timeout: 10_000 });

    // Click the "+" button with title "Create channel"
    await page.getByTitle("Create channel").click();

    // Fill in channel creation form
    await page.getByPlaceholder("e.g. engineering").fill("backend");
    await page
      .getByPlaceholder("What's this channel about?")
      .fill("Backend development");

    // Submit the form
    await page.getByRole("button", { name: "Create" }).click();

    // Channel should appear in the sidebar
    await expect(
      page.locator("aside button", { hasText: "backend" }).first()
    ).toBeVisible({ timeout: 10_000 });
  });
});

test.describe("Space with channels - messaging", () => {
  test("user can send messages in a space channel", async ({ page }) => {
    await apiCreateSpace("Team", admin.token);

    await loginViaToken(page, admin.token);
    await expect(page.getByText("Team")).toBeVisible({ timeout: 10_000 });

    // Create a channel in the space
    await page.getByTitle("Create channel").click();
    await page.getByPlaceholder("e.g. engineering").fill("chat");
    await page.getByRole("button", { name: "Create" }).click();

    // Click on the new channel
    await page
      .locator("aside button", { hasText: "chat" })
      .first()
      .click();

    // Send a message
    const msgInput = page.locator('[role="textbox"][aria-multiline="true"]');
    await msgInput.click();
    await msgInput.pressSequentially("Hello from space!");
    await page.getByRole("button", { name: "Send" }).click();

    // Verify message appears
    await expect(page.getByText("Hello from space!")).toBeVisible({
      timeout: 10_000,
    });
  });
});
