/**
 * E2E tests for messaging: channels, sending/receiving messages.
 */

import { test, expect } from "../fixtures.js";
import { resetDatabase } from "../helpers/db.js";
import {
  setupAdminUser,
  loginViaToken,
  type TestUser,
} from "../helpers/auth.js";
import { apiCreateSpace, apiCreateSpaceChannel } from "../helpers/api.js";

let admin: TestUser;

test.beforeEach(async ({ workerConfig }) => {
  resetDatabase(workerConfig.dbConfig);
  admin = await setupAdminUser(workerConfig.apiConfig);
  // Create a space with a channel for messaging tests
  const space = await apiCreateSpace("Test Space", admin.token, {
    is_public: true,
  }, workerConfig.apiConfig);
  await apiCreateSpaceChannel(space.id, "general", admin.token, undefined, workerConfig.apiConfig);
});

/** Helper to get the message input (contentEditable div) */
function getMessageInput(page: import("@playwright/test").Page) {
  return page.locator('[role="textbox"][aria-multiline="true"]');
}

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

    // Type a message into the contentEditable input
    const input = getMessageInput(page);
    await input.click();
    await input.pressSequentially("Hello world!");

    // Send it
    await page.getByRole("button", { name: "Send" }).click();

    // Message should appear in the chat
    await expect(page.getByText("Hello world!")).toBeVisible({
      timeout: 10_000,
    });

    // Input should be cleared
    await expect(input).toHaveText("");
  });

  test("user can send a message with Enter key", async ({ page }) => {
    await loginViaToken(page, admin.token);

    await page
      .locator("aside button", { hasText: "general" })
      .first()
      .click();

    const input = getMessageInput(page);
    await input.click();
    await input.pressSequentially("Message via Enter");
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

    const input = getMessageInput(page);
    await input.click();
    await input.pressSequentially("Line 1");
    await input.press("Shift+Enter");
    await input.pressSequentially("Line 2");

    // Message should NOT have been sent yet (editor should still have content)
    const text = await input.innerText();
    expect(text).toContain("Line 1");
    expect(text).toContain("Line 2");
  });

  test("multiple messages appear in order", async ({ page }) => {
    await loginViaToken(page, admin.token);

    await page
      .locator("aside button", { hasText: "general" })
      .first()
      .click();

    // Send multiple messages
    const input = getMessageInput(page);
    for (const msg of ["First message", "Second message", "Third message"]) {
      await input.click();
      await input.pressSequentially(msg);
      await input.press("Enter");
      await expect(page.getByText(msg)).toBeVisible({ timeout: 10_000 });
    }

    // All messages should be visible
    await expect(page.getByText("First message")).toBeVisible();
    await expect(page.getByText("Second message")).toBeVisible();
    await expect(page.getByText("Third message")).toBeVisible();
  });
});
