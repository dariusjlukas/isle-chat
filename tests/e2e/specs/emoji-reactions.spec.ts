/**
 * E2E tests for emoji picker and message reactions.
 */

import { test, expect } from "@playwright/test";
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

/** Helper to get the message input (contentEditable div) */
function getMessageInput(page: import("@playwright/test").Page) {
  return page.locator('[role="textbox"][aria-multiline="true"]');
}

/** Navigate to the general channel */
async function goToChannel(page: import("@playwright/test").Page) {
  await page
    .locator("aside button", { hasText: "general" })
    .first()
    .click();
  await expect(page.locator("header").getByText("general")).toBeVisible();
}

/** Send a message via the UI */
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

/** Click the add-reaction button and pick an emoji by clicking at coordinates */
async function pickEmoji(
  page: import("@playwright/test").Page,
  msgBubble: import("@playwright/test").Locator,
) {
  const addReactionBtn = msgBubble.locator(
    'button:has([data-icon="face-smile"])',
  );
  await addReactionBtn.click();

  const picker = page.locator("em-emoji-picker");
  await expect(picker).toBeVisible({ timeout: 5_000 });

  // Wait for the picker to fully render its emoji grid
  await page.waitForTimeout(800);

  // Get the bounding box of an emoji button inside the shadow DOM,
  // then click at those coordinates using Playwright (which triggers proper events)
  const coords = await picker.evaluate((el) => {
    const shadow = el.shadowRoot;
    if (!shadow) return null;
    // Skip the first ~10 buttons (category nav buttons)
    const allButtons = Array.from(shadow.querySelectorAll("button"));
    for (const btn of allButtons.slice(10)) {
      const rect = btn.getBoundingClientRect();
      if (rect.width > 0 && rect.height > 0 && rect.width < 50) {
        return {
          x: rect.x + rect.width / 2,
          y: rect.y + rect.height / 2,
        };
      }
    }
    return null;
  });

  if (coords) {
    await page.mouse.click(coords.x, coords.y);
  } else {
    throw new Error("Could not find any emoji button in the picker");
  }
}

test.beforeEach(async () => {
  resetDatabase();
  admin = await setupAdminUser();
  alice = await setupRegularUser("alice", "Alice");
  const space = await apiCreateSpace("Test Space", admin.token, {
    is_public: true,
  });
  const channel = await apiCreateSpaceChannel(space.id, "general", admin.token);
  // Alice joins the public space and channel so she can see it
  await apiJoinSpace(space.id, alice.token);
  await apiJoinChannel(channel.id, alice.token);
});

test.describe("Emoji picker", () => {
  test("emoji button has a tooltip", async ({ page }) => {
    await loginViaToken(page, admin.token);
    await goToChannel(page);

    const emojiButton = page
      .locator('button:has([data-icon="face-smile"])')
      .first();
    await emojiButton.hover();

    await expect(page.getByRole("tooltip", { name: "Emoji" })).toBeVisible({
      timeout: 5_000,
    });
  });

  test("attach file button has a tooltip", async ({ page }) => {
    await loginViaToken(page, admin.token);
    await goToChannel(page);

    const attachButton = page
      .locator('button:has([data-icon="paperclip"])')
      .first();
    await attachButton.hover();

    await expect(
      page.getByRole("tooltip", { name: "Attach file" }),
    ).toBeVisible({ timeout: 5_000 });
  });

  test("clicking emoji button opens the emoji picker", async ({ page }) => {
    await loginViaToken(page, admin.token);
    await goToChannel(page);

    const emojiButton = page
      .locator('button:has([data-icon="face-smile"])')
      .first();
    await emojiButton.click();

    await expect(page.locator("em-emoji-picker")).toBeVisible({
      timeout: 5_000,
    });
  });

  test("clicking outside the emoji picker closes it", async ({ page }) => {
    await loginViaToken(page, admin.token);
    await goToChannel(page);

    const emojiButton = page
      .locator('button:has([data-icon="face-smile"])')
      .first();
    await emojiButton.click();
    await expect(page.locator("em-emoji-picker")).toBeVisible({
      timeout: 5_000,
    });

    await page.locator("header").click();
    await expect(page.locator("em-emoji-picker")).not.toBeVisible();
  });
});

test.describe("Reactions", () => {
  test("add reaction button appears on other users' messages", async ({
    browser,
  }) => {
    // Admin sends a message via UI
    const adminCtx = await browser.newContext();
    const adminPage = await adminCtx.newPage();
    await loginViaToken(adminPage, admin.token);
    await goToChannel(adminPage);
    await sendMessage(adminPage, "React to this!");

    // Alice views it
    const aliceCtx = await browser.newContext();
    const alicePage = await aliceCtx.newPage();
    await loginViaToken(alicePage, alice.token);
    await goToChannel(alicePage);

    await expect(alicePage.getByText("React to this!")).toBeVisible({
      timeout: 10_000,
    });

    // The add-reaction button should be visible on other users' messages
    const msgBubble = alicePage.locator('[id^="msg-"]').first();
    const addReactionBtn = msgBubble.locator(
      'button:has([data-icon="face-smile"])',
    );
    await expect(addReactionBtn).toBeVisible();

    await adminCtx.close();
    await aliceCtx.close();
  });

  test("add reaction button does NOT appear on own messages", async ({
    page,
  }) => {
    await loginViaToken(page, admin.token);
    await goToChannel(page);
    await sendMessage(page, "My own message");

    // Own message should NOT have an add-reaction button
    const msgBubble = page.locator('[id^="msg-"]').first();
    const addReactionBtn = msgBubble.locator(
      'button:has([data-icon="face-smile"])',
    );
    await expect(addReactionBtn).not.toBeVisible();
  });

  test("user can add a reaction via the emoji picker", async ({ browser }) => {
    // Admin sends a message
    const adminCtx = await browser.newContext();
    const adminPage = await adminCtx.newPage();
    await loginViaToken(adminPage, admin.token);
    await goToChannel(adminPage);
    await sendMessage(adminPage, "Add a reaction here");

    // Alice reacts to it
    const aliceCtx = await browser.newContext();
    const alicePage = await aliceCtx.newPage();
    await loginViaToken(alicePage, alice.token);
    await goToChannel(alicePage);

    await expect(alicePage.getByText("Add a reaction here")).toBeVisible({
      timeout: 10_000,
    });

    const msgBubble = alicePage.locator('[id^="msg-"]').first();
    await pickEmoji(alicePage, msgBubble);

    // The emoji picker should close
    await expect(alicePage.locator("em-emoji-picker")).not.toBeVisible({
      timeout: 3_000,
    });

    // A reaction badge should appear
    const reactionBadge = msgBubble.locator('button:has-text("1")');
    await expect(reactionBadge).toBeVisible({ timeout: 5_000 });

    await adminCtx.close();
    await aliceCtx.close();
  });

  test("reaction badge shows who reacted on hover", async ({ browser }) => {
    // Admin sends a message
    const adminCtx = await browser.newContext();
    const adminPage = await adminCtx.newPage();
    await loginViaToken(adminPage, admin.token);
    await goToChannel(adminPage);
    await sendMessage(adminPage, "Hover test message");

    // Alice reacts
    const aliceCtx = await browser.newContext();
    const alicePage = await aliceCtx.newPage();
    await loginViaToken(alicePage, alice.token);
    await goToChannel(alicePage);

    await expect(alicePage.getByText("Hover test message")).toBeVisible({
      timeout: 10_000,
    });

    const msgBubble = alicePage.locator('[id^="msg-"]').first();
    await pickEmoji(alicePage, msgBubble);

    const reactionBadge = msgBubble.locator('button:has-text("1")');
    await expect(reactionBadge).toBeVisible({ timeout: 5_000 });

    // Hover to check tooltip shows the username
    await reactionBadge.hover();
    await expect(
      alicePage.getByRole("tooltip", { name: "alice" }),
    ).toBeVisible({ timeout: 3_000 });

    await adminCtx.close();
    await aliceCtx.close();
  });

  test("user can toggle off their own reaction", async ({ browser }) => {
    // Admin sends a message
    const adminCtx = await browser.newContext();
    const adminPage = await adminCtx.newPage();
    await loginViaToken(adminPage, admin.token);
    await goToChannel(adminPage);
    await sendMessage(adminPage, "Toggle reaction test");

    // Alice reacts
    const aliceCtx = await browser.newContext();
    const alicePage = await aliceCtx.newPage();
    await loginViaToken(alicePage, alice.token);
    await goToChannel(alicePage);

    await expect(alicePage.getByText("Toggle reaction test")).toBeVisible({
      timeout: 10_000,
    });

    const msgBubble = alicePage.locator('[id^="msg-"]').first();
    await pickEmoji(alicePage, msgBubble);

    // Reaction badge should appear
    const reactionBadge = msgBubble.locator('button:has-text("1")');
    await expect(reactionBadge).toBeVisible({ timeout: 5_000 });

    // Click the reaction badge to remove it
    await reactionBadge.click();

    // The badge should disappear
    await expect(reactionBadge).not.toBeVisible({ timeout: 5_000 });

    await adminCtx.close();
    await aliceCtx.close();
  });

  test("reactions are visible across users in real-time", async ({
    browser,
  }) => {
    // Admin sends a message
    const adminCtx = await browser.newContext();
    const adminPage = await adminCtx.newPage();
    await loginViaToken(adminPage, admin.token);
    await goToChannel(adminPage);
    await sendMessage(adminPage, "Cross-user reaction");

    // Alice opens the channel
    const aliceCtx = await browser.newContext();
    const alicePage = await aliceCtx.newPage();
    await loginViaToken(alicePage, alice.token);
    await goToChannel(alicePage);

    await expect(alicePage.getByText("Cross-user reaction")).toBeVisible({
      timeout: 10_000,
    });

    // Alice adds a reaction
    const aliceMsgBubble = alicePage.locator('[id^="msg-"]').first();
    await pickEmoji(alicePage, aliceMsgBubble);

    // Alice should see the reaction badge
    const aliceReactionBadge = aliceMsgBubble.locator('button:has-text("1")');
    await expect(aliceReactionBadge).toBeVisible({ timeout: 5_000 });

    // Admin should also see the reaction badge in real-time
    const adminMsgBubble = adminPage.locator('[id^="msg-"]').first();
    const adminReactionBadge = adminMsgBubble.locator('button:has-text("1")');
    await expect(adminReactionBadge).toBeVisible({ timeout: 10_000 });

    await adminCtx.close();
    await aliceCtx.close();
  });
});
