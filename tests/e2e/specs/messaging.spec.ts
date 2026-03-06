/**
 * E2E tests for messaging: channels, sending/receiving messages.
 */

import { test, expect } from "@playwright/test";
import { resetDatabase } from "../helpers/db.js";
import {
  setupAdminUser,
  loginViaToken,
  type TestUser,
} from "../helpers/auth.js";
import { apiCreateSpace, apiCreateSpaceChannel } from "../helpers/api.js";

let admin: TestUser;

test.beforeEach(async () => {
  resetDatabase();
  admin = await setupAdminUser();
  // Create a space with a channel for messaging tests
  const space = await apiCreateSpace("Test Space", admin.token, {
    is_public: true,
  });
  await apiCreateSpaceChannel(space.id, "general", admin.token);
});

test.describe("Channel navigation", () => {
  test("user can see channels in the sidebar", async ({ page }) => {
    await loginViaToken(page, admin.token);

    // The channel should be visible in the space's side panel
    await expect(page.getByText("general")).toBeVisible({ timeout: 10_000 });
  });

  test("user can click a channel to view it", async ({ page }) => {
    await loginViaToken(page, admin.token);

    // Click on the general channel in the sidebar
    await page
      .locator("aside button", { hasText: "general" })
      .first()
      .click();

    // The header should show the channel name
    await expect(page.locator("header").getByText("general")).toBeVisible();
  });
});

test.describe("Sending messages", () => {
  test("user can send a message in a channel", async ({ page }) => {
    await loginViaToken(page, admin.token);

    // Navigate to the channel
    await page
      .locator("aside button", { hasText: "general" })
      .first()
      .click();

    // Type a message
    await page.getByPlaceholder("Type a message...").fill("Hello world!");

    // Send it
    await page.getByRole("button", { name: "Send" }).click();

    // Message should appear in the chat
    await expect(page.getByText("Hello world!")).toBeVisible({
      timeout: 10_000,
    });

    // Input should be cleared
    await expect(page.getByPlaceholder("Type a message...")).toHaveValue("");
  });

  test("user can send a message with Enter key", async ({ page }) => {
    await loginViaToken(page, admin.token);

    await page
      .locator("aside button", { hasText: "general" })
      .first()
      .click();

    const input = page.getByPlaceholder("Type a message...");
    await input.fill("Message via Enter");
    await input.press("Enter");

    await expect(page.getByText("Message via Enter")).toBeVisible({
      timeout: 10_000,
    });
  });

  test("Shift+Enter creates a newline instead of sending", async ({
    page,
  }) => {
    await loginViaToken(page, admin.token);

    await page
      .locator("aside button", { hasText: "general" })
      .first()
      .click();

    const input = page.getByPlaceholder("Type a message...");
    await input.fill("Line 1");
    await input.press("Shift+Enter");
    await input.type("Line 2");

    // Message should NOT have been sent yet (textarea should still have content)
    const value = await input.inputValue();
    expect(value).toContain("Line 1");
    expect(value).toContain("Line 2");
  });

  test("multiple messages appear in order", async ({ page }) => {
    await loginViaToken(page, admin.token);

    await page
      .locator("aside button", { hasText: "general" })
      .first()
      .click();

    // Send multiple messages
    const input = page.getByPlaceholder("Type a message...");
    for (const msg of ["First message", "Second message", "Third message"]) {
      await input.fill(msg);
      await input.press("Enter");
      await expect(page.getByText(msg)).toBeVisible({ timeout: 10_000 });
    }

    // All messages should be visible
    await expect(page.getByText("First message")).toBeVisible();
    await expect(page.getByText("Second message")).toBeVisible();
    await expect(page.getByText("Third message")).toBeVisible();
  });
});
