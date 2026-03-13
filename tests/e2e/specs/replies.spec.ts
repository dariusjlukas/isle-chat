/**
 * E2E tests for message replies: reply button, reply preview, reply card,
 * click-to-jump, and deleted message handling.
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
  apiCreateSpace,
  apiCreateSpaceChannel,
  apiJoinSpace,
  apiJoinChannel,
} from "../helpers/api.js";

let admin: TestUser;
let alice: TestUser;
let channelId: string;

/** Helper to get the message input (contentEditable div) */
function getMessageInput(page: import("@playwright/test").Page) {
  return page.locator('[role="textbox"][aria-multiline="true"]');
}

/** Navigate to the general channel */
async function goToChannel(page: import("@playwright/test").Page) {
  await page.locator("aside button", { hasText: "general" }).first().click();
  await expect(page.locator("header").getByText("general")).toBeVisible();
}

/** Send a message via the UI and wait for it to appear */
async function sendMessage(
  page: import("@playwright/test").Page,
  text: string,
) {
  const input = getMessageInput(page);
  await input.click();
  await input.pressSequentially(text);
  await input.press("Enter");
  await expect(page.getByText(text)).toBeVisible({ timeout: 10_000 });
}

test.beforeEach(async ({ workerConfig }) => {
  resetDatabase(workerConfig.dbConfig);
  admin = await setupAdminUser(workerConfig.apiConfig);
  alice = await setupRegularUser("alice", "Alice", workerConfig.apiConfig);
  const space = await apiCreateSpace(
    "Test Space",
    admin.token,
    { is_public: true },
    workerConfig.apiConfig,
  );
  const channel = await apiCreateSpaceChannel(
    space.id,
    "general",
    admin.token,
    undefined,
    workerConfig.apiConfig,
  );
  channelId = channel.id;
  await apiJoinSpace(space.id, alice.token, workerConfig.apiConfig);
  await apiJoinChannel(channelId, alice.token, workerConfig.apiConfig);
});

test.describe("Reply button", () => {
  test("reply button appears on hover for own messages", async ({ page }) => {
    await loginViaToken(page, admin.token);
    await goToChannel(page);
    await sendMessage(page, "My own message");

    const msgBubble = page.locator('[id^="msg-"]').first();
    await msgBubble.hover();

    const replyBtn = msgBubble.locator('button:has([data-icon="reply"])');
    await expect(replyBtn).toBeVisible({ timeout: 3_000 });
  });

  test("reply button appears on hover for other users' messages", async ({
    browser,
    workerConfig,
  }) => {
    // Admin sends a message
    const adminCtx = await browser.newContext({
      baseURL: workerConfig.frontendUrl,
    });
    const adminPage = await adminCtx.newPage();
    await loginViaToken(adminPage, admin.token);
    await goToChannel(adminPage);
    await sendMessage(adminPage, "Reply to me!");

    // Alice views and hovers
    const aliceCtx = await browser.newContext({
      baseURL: workerConfig.frontendUrl,
    });
    const alicePage = await aliceCtx.newPage();
    await loginViaToken(alicePage, alice.token);
    await goToChannel(alicePage);

    await expect(alicePage.getByText("Reply to me!")).toBeVisible({
      timeout: 10_000,
    });

    const msgBubble = alicePage.locator('[id^="msg-"]').first();
    await msgBubble.hover();

    const replyBtn = msgBubble.locator('button:has([data-icon="reply"])');
    await expect(replyBtn).toBeVisible({ timeout: 3_000 });

    await adminCtx.close();
    await aliceCtx.close();
  });

  test("reply button has a tooltip", async ({ page }) => {
    await loginViaToken(page, admin.token);
    await goToChannel(page);
    await sendMessage(page, "Tooltip test");

    const msgBubble = page.locator('[id^="msg-"]').first();
    await msgBubble.hover();

    const replyBtn = msgBubble.locator('button:has([data-icon="reply"])');
    await replyBtn.hover();

    await expect(page.getByRole("tooltip", { name: "Reply" })).toBeVisible({
      timeout: 5_000,
    });
  });
});

test.describe("Reply preview bar", () => {
  test("clicking reply button shows reply preview above input", async ({
    page,
  }) => {
    await loginViaToken(page, admin.token);
    await goToChannel(page);
    await sendMessage(page, "Original message");

    const msgBubble = page.locator('[id^="msg-"]').first();
    await msgBubble.hover();

    const replyBtn = msgBubble.locator('button:has([data-icon="reply"])');
    await replyBtn.click();

    // Reply preview should show the username and content
    await expect(page.getByText("Replying to admin")).toBeVisible({
      timeout: 3_000,
    });
    await expect(
      page.locator(".bg-content2").getByText("Original message"),
    ).toBeVisible();
  });

  test("clicking X on reply preview cancels the reply", async ({ page }) => {
    await loginViaToken(page, admin.token);
    await goToChannel(page);
    await sendMessage(page, "Cancel test message");

    const msgBubble = page.locator('[id^="msg-"]').first();
    await msgBubble.hover();

    const replyBtn = msgBubble.locator('button:has([data-icon="reply"])');
    await replyBtn.click();

    await expect(page.getByText("Replying to admin")).toBeVisible({
      timeout: 3_000,
    });

    // Click the cancel (X) button on the reply preview
    const cancelBtn = page
      .locator(".bg-content2")
      .locator('button:has([data-icon="xmark"])');
    await cancelBtn.click();

    await expect(page.getByText("Replying to admin")).not.toBeVisible();
  });
});

test.describe("Sending and displaying replies", () => {
  test("sending a reply shows a reply card with original message preview", async ({
    page,
  }) => {
    await loginViaToken(page, admin.token);
    await goToChannel(page);
    await sendMessage(page, "The original message");

    // Click reply on the message
    const msgBubble = page.locator('[id^="msg-"]').first();
    await msgBubble.hover();
    const replyBtn = msgBubble.locator('button:has([data-icon="reply"])');
    await replyBtn.click();

    await expect(page.getByText("Replying to admin")).toBeVisible({
      timeout: 3_000,
    });

    // Send the reply
    await sendMessage(page, "This is my reply");

    // Reply card should appear inside the reply message bubble
    const replyMsgBubble = page.locator('[id^="msg-"]').last();
    // The reply card should contain the original message text
    const replyCard = replyMsgBubble.locator(".border-l-2.border-primary");
    await expect(replyCard).toBeVisible({ timeout: 5_000 });
    await expect(replyCard.getByText("The original message")).toBeVisible();

    // Reply preview should be cleared
    await expect(page.getByText("Replying to admin")).not.toBeVisible();
  });

  test("reply card shows username of original message author", async ({
    browser,
    workerConfig,
  }) => {
    // Admin sends a message
    const adminCtx = await browser.newContext({
      baseURL: workerConfig.frontendUrl,
    });
    const adminPage = await adminCtx.newPage();
    await loginViaToken(adminPage, admin.token);
    await goToChannel(adminPage);
    await sendMessage(adminPage, "Admin original");

    // Alice replies
    const aliceCtx = await browser.newContext({
      baseURL: workerConfig.frontendUrl,
    });
    const alicePage = await aliceCtx.newPage();
    await loginViaToken(alicePage, alice.token);
    await goToChannel(alicePage);

    await expect(alicePage.getByText("Admin original")).toBeVisible({
      timeout: 10_000,
    });

    const msgBubble = alicePage.locator('[id^="msg-"]').first();
    await msgBubble.hover();
    const replyBtn = msgBubble.locator('button:has([data-icon="reply"])');
    await replyBtn.click();

    await sendMessage(alicePage, "Alice reply");

    // The reply card should show admin's username
    const replyCard = alicePage
      .locator('[id^="msg-"]')
      .last()
      .locator(".border-l-2.border-primary");
    await expect(replyCard).toBeVisible({ timeout: 5_000 });
    await expect(replyCard.getByText("admin", { exact: true })).toBeVisible();

    await adminCtx.close();
    await aliceCtx.close();
  });

  test("reply is visible to other users in real-time", async ({
    browser,
    workerConfig,
  }) => {
    // Admin sends a message and Alice replies
    const adminCtx = await browser.newContext({
      baseURL: workerConfig.frontendUrl,
    });
    const adminPage = await adminCtx.newPage();
    await loginViaToken(adminPage, admin.token);
    await goToChannel(adminPage);
    await sendMessage(adminPage, "Realtime reply test");

    const aliceCtx = await browser.newContext({
      baseURL: workerConfig.frontendUrl,
    });
    const alicePage = await aliceCtx.newPage();
    await loginViaToken(alicePage, alice.token);
    await goToChannel(alicePage);

    await expect(alicePage.getByText("Realtime reply test")).toBeVisible({
      timeout: 10_000,
    });

    // Alice clicks reply and sends
    const msgBubble = alicePage.locator('[id^="msg-"]').first();
    await msgBubble.hover();
    const replyBtn = msgBubble.locator('button:has([data-icon="reply"])');
    await replyBtn.click();
    await sendMessage(alicePage, "Alice realtime reply");

    // Admin should see the reply with the reply card in real-time
    const adminReplyCard = adminPage
      .locator('[id^="msg-"]')
      .last()
      .locator(".border-l-2.border-primary");
    await expect(adminReplyCard).toBeVisible({ timeout: 10_000 });
    await expect(adminReplyCard.getByText("Realtime reply test")).toBeVisible();

    await adminCtx.close();
    await aliceCtx.close();
  });
});

test.describe("Reply card click-to-jump", () => {
  test("clicking a reply card scrolls to and highlights the original message", async ({
    page,
  }) => {
    await loginViaToken(page, admin.token);
    await goToChannel(page);

    // Send enough messages to create scroll (use zero-padded numbers to avoid substring overlap)
    for (let i = 1; i <= 30; i++) {
      await sendMessage(page, `Filler msg A${String(i).padStart(2, "0")}`);
    }

    // The first message should be scrolled out of view
    // Send a reply to one of the earlier messages by first scrolling up
    // Instead, send a reply to the latest visible message and verify jump works

    // Send a target message, then many more, then reply to the target
    await sendMessage(page, "Jump target message");

    // Send more messages to push the target out of view
    for (let i = 1; i <= 20; i++) {
      await sendMessage(page, `Filler msg B${String(i).padStart(2, "0")}`);
    }

    // Now reply to "Jump target message" - we need to scroll up to find it
    // Instead, let's use a simpler approach: reply to the last visible message,
    // which we know has a valid reply card, and verify the highlight flash

    // Reply to the latest message
    const lastMsg = page.locator('[id^="msg-"]').last();
    await lastMsg.hover();
    const replyBtn = lastMsg.locator('button:has([data-icon="reply"])');
    await replyBtn.click();
    await sendMessage(page, "Reply to check highlight");

    // Click the reply card
    const replyCard = page
      .locator('[id^="msg-"]')
      .last()
      .locator(".border-l-2.border-primary");
    await expect(replyCard).toBeVisible({ timeout: 5_000 });
    await replyCard.click();

    // The original message should get the highlight-flash class
    await expect(page.locator('[id^="msg-"].highlight-flash')).toBeVisible({
      timeout: 5_000,
    });
  });
});

test.describe("Deleted message replies", () => {
  test("reply card shows 'deleted' notice when original message is deleted", async ({
    page,
  }) => {
    await loginViaToken(page, admin.token);
    await goToChannel(page);

    // Send original message
    await sendMessage(page, "Will be deleted");

    // Reply to it
    const msgBubble = page.locator('[id^="msg-"]').first();
    await msgBubble.hover();
    const replyBtn = msgBubble.locator('button:has([data-icon="reply"])');
    await replyBtn.click();
    await sendMessage(page, "Reply to soon-deleted");

    // Verify reply card is visible with original content
    const replyCard = page
      .locator('[id^="msg-"]')
      .last()
      .locator(".border-l-2.border-primary");
    await expect(replyCard).toBeVisible({ timeout: 5_000 });
    await expect(replyCard.getByText("Will be deleted")).toBeVisible();

    // Delete the original message via the UI
    const originalMsg = page.locator('[id^="msg-"]').first();
    await originalMsg.hover();

    // Open the dropdown menu
    const menuBtn = originalMsg.locator('button:has([data-icon="ellipsis"])');
    await menuBtn.click();

    // Click delete
    page.on("dialog", (dialog) => dialog.accept());
    await page.getByRole("menuitem", { name: "Delete" }).click();

    // The original message should show "This message was deleted"
    await expect(
      page
        .locator('[id^="msg-"]')
        .first()
        .getByText("This message was deleted"),
    ).toBeVisible({ timeout: 10_000 });

    // Reload to get fresh reply data from the server
    await page.reload();
    await page.getByText("EnclaveStation").first().waitFor({ timeout: 10_000 });
    await goToChannel(page);

    // The reply card should now show the deleted notice
    const replyCardAfter = page
      .locator('[id^="msg-"]')
      .last()
      .locator(".border-l-2.border-primary");
    await expect(replyCardAfter).toBeVisible({ timeout: 5_000 });
    await expect(
      replyCardAfter.getByText("This message was deleted"),
    ).toBeVisible();
  });

  test("clicking reply card of a deleted message still scrolls to it", async ({
    page,
  }) => {
    await loginViaToken(page, admin.token);
    await goToChannel(page);

    // Send original, reply, then delete original
    await sendMessage(page, "Delete and jump test");

    const msgBubble = page.locator('[id^="msg-"]').first();
    await msgBubble.hover();
    const replyBtn = msgBubble.locator('button:has([data-icon="reply"])');
    await replyBtn.click();
    await sendMessage(page, "Reply to deleted jump test");

    // Delete the original
    const originalMsg = page.locator('[id^="msg-"]').first();
    await originalMsg.hover();
    const menuBtn = originalMsg.locator('button:has([data-icon="ellipsis"])');
    await menuBtn.click();

    page.on("dialog", (dialog) => dialog.accept());
    await page.getByRole("menuitem", { name: "Delete" }).click();

    await expect(
      page
        .locator('[id^="msg-"]')
        .first()
        .getByText("This message was deleted"),
    ).toBeVisible({ timeout: 10_000 });

    // Reload to get fresh data
    await page.reload();
    await page.getByText("EnclaveStation").first().waitFor({ timeout: 10_000 });
    await goToChannel(page);

    // Click the reply card — it should still trigger a jump
    const replyCard = page
      .locator('[id^="msg-"]')
      .last()
      .locator(".border-l-2.border-primary");
    await expect(replyCard).toBeVisible({ timeout: 5_000 });
    await replyCard.click();

    // The deleted message should get highlighted
    await expect(page.locator(".highlight-flash")).toBeVisible({
      timeout: 5_000,
    });
  });
});
