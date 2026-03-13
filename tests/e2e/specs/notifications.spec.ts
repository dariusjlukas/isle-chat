/**
 * E2E tests for the notifications feature: bell icon, unread badge,
 * notification dropdown, click-to-navigate, and mark-all-read.
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
let spaceName: string;

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

/** Click the notification bell button */
async function openNotifications(page: import("@playwright/test").Page) {
  const bellBtn = page.locator('button:has([data-icon="bell"])');
  await bellBtn.click();
  await expect(page.getByText("Notifications", { exact: true })).toBeVisible({ timeout: 5_000 });
}

/** Get the notification bell button */
function getBellButton(page: import("@playwright/test").Page) {
  return page.locator('button:has([data-icon="bell"])');
}

/** Navigate to the space panel via the icon rail (without selecting a channel) */
async function goToSpacePanel(page: import("@playwright/test").Page) {
  // Space buttons in the icon rail don't have FontAwesome data-icon attributes
  // (unlike the Messages button with "comment" and the Add button with "plus")
  const iconRail = page.locator(".w-16.flex-shrink-0");
  const spaceBtn = iconRail.locator("button:not(:has([data-icon]))").first();
  await expect(spaceBtn).toBeVisible({ timeout: 10_000 });
  await spaceBtn.click();
}

/** Navigate to the Messages (DMs) view to deselect any active channel */
async function goToMessagesView(page: import("@playwright/test").Page) {
  const iconRail = page.locator(".w-16.flex-shrink-0");
  const messagesBtn = iconRail.locator('button:has([data-icon="comment"])');
  await messagesBtn.click();
}

test.beforeEach(async ({ workerConfig }) => {
  resetDatabase(workerConfig.dbConfig);
  admin = await setupAdminUser(workerConfig.apiConfig);
  alice = await setupRegularUser("alice", "Alice", workerConfig.apiConfig);
  spaceName = "Notif Space";
  const space = await apiCreateSpace(
    spaceName,
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

test.describe("Notification bell", () => {
  test("bell icon is visible in the header", async ({ page }) => {
    await loginViaToken(page, admin.token);
    const bell = getBellButton(page);
    await expect(bell).toBeVisible();
  });

  test("clicking bell opens notification dropdown", async ({ page }) => {
    await loginViaToken(page, admin.token);
    await openNotifications(page);
    await expect(page.getByText("No notifications")).toBeVisible();
  });

  test("clicking outside closes the dropdown", async ({ page }) => {
    await loginViaToken(page, admin.token);
    await openNotifications(page);
    // Click outside the dropdown
    await page.locator("header").click({ position: { x: 10, y: 10 } });
    await expect(page.getByText("No notifications")).not.toBeVisible();
  });
});

test.describe("Mention notifications", () => {
  test("@mention creates a notification for the mentioned user", async ({
    browser,
    workerConfig,
  }) => {
    // Admin sends a message mentioning Alice
    const adminCtx = await browser.newContext({
      baseURL: workerConfig.frontendUrl,
    });
    const adminPage = await adminCtx.newPage();
    await loginViaToken(adminPage, admin.token);
    await goToChannel(adminPage);
    await sendMessage(adminPage, "Hey @alice check this out");

    // Alice logs in and checks notifications
    const aliceCtx = await browser.newContext({
      baseURL: workerConfig.frontendUrl,
    });
    const alicePage = await aliceCtx.newPage();
    await loginViaToken(alicePage, alice.token);

    // Wait a moment for the WebSocket notification_count to arrive
    await alicePage.waitForTimeout(1000);

    // The bell should show an unread badge
    const badge = alicePage.locator(
      'button:has([data-icon="bell"]) span.bg-danger',
    );
    await expect(badge).toBeVisible({ timeout: 10_000 });
    await expect(badge).toHaveText("1");

    // Open the dropdown
    await openNotifications(alicePage);

    // Should show the mention notification
    await expect(alicePage.getByText("mentioned you")).toBeVisible({
      timeout: 5_000,
    });
    await expect(
      alicePage.getByText("Hey @alice check this out"),
    ).toBeVisible();

    // Unread dot should be present
    const unreadDot = alicePage.locator(
      ".bg-danger.rounded-full.w-3.h-3",
    );
    await expect(unreadDot).toBeVisible();

    await adminCtx.close();
    await aliceCtx.close();
  });

  test("clicking a notification navigates to the source message", async ({
    browser,
    workerConfig,
  }) => {
    // Admin sends a mention message
    const adminCtx = await browser.newContext({
      baseURL: workerConfig.frontendUrl,
    });
    const adminPage = await adminCtx.newPage();
    await loginViaToken(adminPage, admin.token);
    await goToChannel(adminPage);
    await sendMessage(adminPage, "Hey @alice navigate-test");

    // Alice opens notifications and clicks the notification
    const aliceCtx = await browser.newContext({
      baseURL: workerConfig.frontendUrl,
    });
    const alicePage = await aliceCtx.newPage();
    await loginViaToken(alicePage, alice.token);

    await alicePage.waitForTimeout(1000);
    await openNotifications(alicePage);

    // Click the notification entry
    await alicePage.getByText("mentioned you").click();

    // Should navigate to the channel and show the message
    await expect(
      alicePage.getByText("Hey @alice navigate-test"),
    ).toBeVisible({ timeout: 10_000 });

    // The dropdown should close after clicking
    await expect(
      alicePage.getByText("mentioned you"),
    ).not.toBeVisible();

    await adminCtx.close();
    await aliceCtx.close();
  });

  test("@channel mention notifies all channel members", async ({
    browser,
    workerConfig,
  }) => {
    // Admin sends @channel
    const adminCtx = await browser.newContext({
      baseURL: workerConfig.frontendUrl,
    });
    const adminPage = await adminCtx.newPage();
    await loginViaToken(adminPage, admin.token);
    await goToChannel(adminPage);
    await sendMessage(adminPage, "Attention @channel important update");

    // Alice should get a notification
    const aliceCtx = await browser.newContext({
      baseURL: workerConfig.frontendUrl,
    });
    const alicePage = await aliceCtx.newPage();
    await loginViaToken(alicePage, alice.token);

    await alicePage.waitForTimeout(1000);

    const badge = alicePage.locator(
      'button:has([data-icon="bell"]) span.bg-danger',
    );
    await expect(badge).toBeVisible({ timeout: 10_000 });

    await openNotifications(alicePage);
    await expect(alicePage.getByText("mentioned you")).toBeVisible({
      timeout: 5_000,
    });

    await adminCtx.close();
    await aliceCtx.close();
  });
});

test.describe("Reply notifications", () => {
  test("replying to a message creates a notification for the original author", async ({
    browser,
    workerConfig,
  }) => {
    // Admin sends a message, then closes context so they aren't viewing the channel
    let adminCtx = await browser.newContext({
      baseURL: workerConfig.frontendUrl,
    });
    let adminPage = await adminCtx.newPage();
    await loginViaToken(adminPage, admin.token);
    await goToChannel(adminPage);
    await sendMessage(adminPage, "Reply notification test");
    await adminCtx.close();

    // Alice replies to admin's message
    const aliceCtx = await browser.newContext({
      baseURL: workerConfig.frontendUrl,
    });
    const alicePage = await aliceCtx.newPage();
    await loginViaToken(alicePage, alice.token);
    await goToChannel(alicePage);

    await expect(
      alicePage.getByText("Reply notification test"),
    ).toBeVisible({ timeout: 10_000 });

    // Click reply on admin's message
    const msgBubble = alicePage.locator('[id^="msg-"]').first();
    await msgBubble.hover();
    const replyBtn = msgBubble.locator('button:has([data-icon="reply"])');
    await replyBtn.click();
    await sendMessage(alicePage, "Here is my reply");

    // Admin reopens and should see the notification
    adminCtx = await browser.newContext({
      baseURL: workerConfig.frontendUrl,
    });
    adminPage = await adminCtx.newPage();
    await loginViaToken(adminPage, admin.token);

    const badge = adminPage.locator(
      'button:has([data-icon="bell"]) span.bg-danger',
    );
    await expect(badge).toBeVisible({ timeout: 10_000 });

    await openNotifications(adminPage);
    await expect(
      adminPage.getByText("replied to your message"),
    ).toBeVisible({ timeout: 5_000 });
    // Scope to the notification dropdown to avoid matching the chat message too
    const dropdown = adminPage.locator(".absolute.top-full");
    await expect(dropdown.getByText("Here is my reply")).toBeVisible();

    await adminCtx.close();
    await aliceCtx.close();
  });
});

test.describe("Mark all as read", () => {
  test("mark all as read clears unread badge and dots", async ({
    browser,
    workerConfig,
  }) => {
    // Admin mentions Alice twice
    const adminCtx = await browser.newContext({
      baseURL: workerConfig.frontendUrl,
    });
    const adminPage = await adminCtx.newPage();
    await loginViaToken(adminPage, admin.token);
    await goToChannel(adminPage);
    await sendMessage(adminPage, "First @alice ping");
    await sendMessage(adminPage, "Second @alice ping");

    // Alice opens notifications
    const aliceCtx = await browser.newContext({
      baseURL: workerConfig.frontendUrl,
    });
    const alicePage = await aliceCtx.newPage();
    await loginViaToken(alicePage, alice.token);

    await alicePage.waitForTimeout(1000);

    const badge = alicePage.locator(
      'button:has([data-icon="bell"]) span.bg-danger',
    );
    await expect(badge).toBeVisible({ timeout: 10_000 });

    await openNotifications(alicePage);

    // Should see "Mark all as read" button
    const markAllBtn = alicePage.getByRole("button", {
      name: "Mark all as read",
    });
    await expect(markAllBtn).toBeVisible();

    // Click it
    await markAllBtn.click();

    // Badge should disappear
    await expect(badge).not.toBeVisible({ timeout: 5_000 });

    // Unread dots should disappear
    const unreadDots = alicePage.locator(".bg-danger.rounded-full.w-3.h-3");
    await expect(unreadDots).toHaveCount(0, { timeout: 5_000 });

    // Mark all button should disappear (no more unread)
    await expect(markAllBtn).not.toBeVisible();

    await adminCtx.close();
    await aliceCtx.close();
  });
});

test.describe("Dismissing notification syncs channel badges", () => {
  test("marking a mention notification as read decrements the space channel badge", async ({
    browser,
    workerConfig,
  }) => {
    // Admin sends two mentions to Alice
    const adminCtx = await browser.newContext({
      baseURL: workerConfig.frontendUrl,
    });
    const adminPage = await adminCtx.newPage();
    await loginViaToken(adminPage, admin.token);
    await goToChannel(adminPage);
    await sendMessage(adminPage, "First @alice ping");
    await sendMessage(adminPage, "Second @alice ping");

    // Alice logs in — she is NOT viewing the space channel, so badges accumulate
    const aliceCtx = await browser.newContext({
      baseURL: workerConfig.frontendUrl,
    });
    const alicePage = await aliceCtx.newPage();
    await loginViaToken(alicePage, alice.token);

    // Navigate to the space panel (without selecting a channel)
    await goToSpacePanel(alicePage);

    // Wait for the mention badge on the general channel to show @2
    const channelBadge = alicePage.locator(
      'button:has-text("general") span.bg-danger',
    );
    await expect(channelBadge).toBeVisible({ timeout: 10_000 });
    await expect(channelBadge).toHaveText("@2");

    // Open notifications and dismiss one by clicking the checkmark button
    await openNotifications(alicePage);
    const notifRows = alicePage.locator(".notif-row");
    await expect(notifRows.first()).toBeVisible({ timeout: 5_000 });

    // Click the dismiss button (the small dot / checkmark) on the first notification
    const dismissBtn = notifRows.first().locator("button[title='Mark as read']");
    await dismissBtn.click();

    // The space channel badge should decrement to @1
    await expect(channelBadge).toHaveText("@1", { timeout: 5_000 });

    await adminCtx.close();
    await aliceCtx.close();
  });

  test("mark all as read clears all space channel badges", async ({
    browser,
    workerConfig,
  }) => {
    // Admin sends mentions to Alice
    const adminCtx = await browser.newContext({
      baseURL: workerConfig.frontendUrl,
    });
    const adminPage = await adminCtx.newPage();
    await loginViaToken(adminPage, admin.token);
    await goToChannel(adminPage);
    await sendMessage(adminPage, "Hey @alice first");
    await sendMessage(adminPage, "Hey @alice second");

    // Alice logs in and navigates to the space panel
    const aliceCtx = await browser.newContext({
      baseURL: workerConfig.frontendUrl,
    });
    const alicePage = await aliceCtx.newPage();
    await loginViaToken(alicePage, alice.token);

    await goToSpacePanel(alicePage);

    // Wait for channel badge
    const channelBadge = alicePage.locator(
      'button:has-text("general") span.bg-danger',
    );
    await expect(channelBadge).toBeVisible({ timeout: 10_000 });

    // Open notifications and click "Mark all as read"
    await openNotifications(alicePage);
    const markAllBtn = alicePage.getByRole("button", {
      name: "Mark all as read",
    });
    await expect(markAllBtn).toBeVisible({ timeout: 5_000 });
    await markAllBtn.click();

    // The channel badge should disappear
    await expect(channelBadge).not.toBeVisible({ timeout: 5_000 });

    await adminCtx.close();
    await aliceCtx.close();
  });

  test("clicking a notification to navigate clears the channel badge", async ({
    browser,
    workerConfig,
  }) => {
    // Admin sends mentions to Alice
    const adminCtx = await browser.newContext({
      baseURL: workerConfig.frontendUrl,
    });
    const adminPage = await adminCtx.newPage();
    await loginViaToken(adminPage, admin.token);
    await goToChannel(adminPage);
    await sendMessage(adminPage, "First @alice click-nav");
    await sendMessage(adminPage, "Second @alice click-nav");

    // Alice logs in, navigates to the space panel to see channel badge
    const aliceCtx = await browser.newContext({
      baseURL: workerConfig.frontendUrl,
    });
    const alicePage = await aliceCtx.newPage();
    await loginViaToken(alicePage, alice.token);

    await goToSpacePanel(alicePage);

    const channelBadge = alicePage.locator(
      'button:has-text("general") span.bg-danger',
    );
    await expect(channelBadge).toBeVisible({ timeout: 10_000 });
    await expect(channelBadge).toHaveText("@2");

    // Open notifications and click one notification to navigate
    await openNotifications(alicePage);
    const firstNotif = alicePage.locator(".notif-row").first();
    await expect(firstNotif).toBeVisible({ timeout: 5_000 });
    await firstNotif.click();

    // Clicking marks the notification as read (decrementing badge) AND
    // navigates to the channel (which clears all unreads for it).
    // So the badge should disappear entirely.
    await expect(channelBadge).not.toBeVisible({ timeout: 5_000 });

    await adminCtx.close();
    await aliceCtx.close();
  });

  test("dismissing a reply notification decrements the icon rail space badge", async ({
    browser,
    workerConfig,
  }) => {
    // Alice sends a message, then closes context so she isn't viewing the channel
    let aliceCtx = await browser.newContext({
      baseURL: workerConfig.frontendUrl,
    });
    let alicePage = await aliceCtx.newPage();
    await loginViaToken(alicePage, alice.token);
    await goToChannel(alicePage);
    await sendMessage(alicePage, "Alice original message");
    await aliceCtx.close();

    // Admin replies to Alice's message
    const adminCtx = await browser.newContext({
      baseURL: workerConfig.frontendUrl,
    });
    const adminPage = await adminCtx.newPage();
    await loginViaToken(adminPage, admin.token);
    await goToChannel(adminPage);

    await expect(
      adminPage.getByText("Alice original message"),
    ).toBeVisible({ timeout: 10_000 });

    // Click reply on Alice's message
    const msgBubble = adminPage.locator('[id^="msg-"]').first();
    await msgBubble.hover();
    const replyBtn = msgBubble.locator('button:has([data-icon="reply"])');
    await replyBtn.click();
    await sendMessage(adminPage, "Admin reply to alice");

    // Alice reopens — she should see the notification badge
    aliceCtx = await browser.newContext({
      baseURL: workerConfig.frontendUrl,
    });
    alicePage = await aliceCtx.newPage();
    await loginViaToken(alicePage, alice.token);

    const bellBadge = alicePage.locator(
      'button:has([data-icon="bell"]) span.bg-danger',
    );
    await expect(bellBadge).toBeVisible({ timeout: 10_000 });

    // Open notifications and dismiss the reply notification
    await openNotifications(alicePage);
    const notifRows = alicePage.locator(".notif-row");
    await expect(
      alicePage.getByText("replied to your message"),
    ).toBeVisible({ timeout: 5_000 });

    const dismissBtn = notifRows.first().locator("button[title='Mark as read']");
    await dismissBtn.click();

    // Bell badge should disappear (was 1, now 0)
    await expect(bellBadge).not.toBeVisible({ timeout: 5_000 });

    await adminCtx.close();
    await aliceCtx.close();
  });
});

test.describe("Real-time notifications", () => {
  test("notification appears in real-time while dropdown is closed", async ({
    browser,
    workerConfig,
  }) => {
    // Alice is already logged in
    const aliceCtx = await browser.newContext({
      baseURL: workerConfig.frontendUrl,
    });
    const alicePage = await aliceCtx.newPage();
    await loginViaToken(alicePage, alice.token);

    // Initially no badge
    const badge = alicePage.locator(
      'button:has([data-icon="bell"]) span.bg-danger',
    );

    // Admin sends a mention
    const adminCtx = await browser.newContext({
      baseURL: workerConfig.frontendUrl,
    });
    const adminPage = await adminCtx.newPage();
    await loginViaToken(adminPage, admin.token);
    await goToChannel(adminPage);
    await sendMessage(adminPage, "Realtime @alice notification");

    // Badge should appear on Alice's page in real-time
    await expect(badge).toBeVisible({ timeout: 10_000 });
    await expect(badge).toHaveText("1");

    await adminCtx.close();
    await aliceCtx.close();
  });
});
