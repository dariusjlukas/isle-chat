/**
 * E2E tests for default-join channels:
 * - Toggle appears in channel settings for space channels
 * - New members auto-join default channels when joining a space
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
  apiUpdateChannel,
  apiGetChannels,
  apiJoinSpace,
} from "../helpers/api.js";

test.beforeEach(({ workerConfig }) => {
  resetDatabase(workerConfig.dbConfig);
});

test.describe("Default-join channel settings toggle", () => {
  test("admin sees auto-join toggle for space channels", async ({
    page,
    workerConfig,
  }) => {
    const admin = await setupAdminUser(workerConfig.apiConfig);
    const space = await apiCreateSpace("Team", admin.token, {
      is_public: true,
    }, workerConfig.apiConfig);
    await apiCreateSpaceChannel(
      space.id,
      "general",
      admin.token,
      undefined,
      workerConfig.apiConfig,
    );

    await loginViaToken(page, admin.token);

    // Navigate to the space
    await page.getByText("Team").first().click();
    // Click on the general channel
    await page.getByText("general").first().click();

    // Open channel settings
    await page.getByTitle("Channel Settings").click();

    // Should see the auto-join toggle in settings
    await expect(
      page.getByText("Auto-Join for New Members"),
    ).toBeVisible({ timeout: 5_000 });
  });

  test("admin can toggle default-join on and save", async ({
    page,
    workerConfig,
  }) => {
    const admin = await setupAdminUser(workerConfig.apiConfig);
    const space = await apiCreateSpace("Team", admin.token, {
      is_public: true,
    }, workerConfig.apiConfig);
    const channel = await apiCreateSpaceChannel(
      space.id,
      "announcements",
      admin.token,
      undefined,
      workerConfig.apiConfig,
    );

    await loginViaToken(page, admin.token);

    // Navigate to space > channel
    await page.getByText("Team").first().click();
    await page.getByText("announcements").first().click();

    // Open channel settings
    await page.getByTitle("Channel Settings").click();

    // Toggle auto-join on
    const toggle = page.getByRole("switch", { name: /auto-join/i }).or(
      page.locator("text=Auto-Join for New Members").locator("..").locator("..").getByRole("switch"),
    );
    await toggle.first().click();

    // Save
    await page.getByRole("button", { name: "Save Settings" }).click();

    // Verify via API that default_join is now true
    const channels = await apiGetChannels(admin.token, workerConfig.apiConfig);
    const ch = channels.find((c) => c.id === channel.id) as { id: string; name: string; default_join?: boolean };
    expect(ch?.default_join).toBe(true);
  });
});

test.describe("Auto-join on space join", () => {
  test("new member sees default-join channels after joining space", async ({
    page,
    workerConfig,
  }) => {
    const admin = await setupAdminUser(workerConfig.apiConfig);
    const regularUser = await setupRegularUser("joiner", "Joiner User", workerConfig.apiConfig);

    // Admin creates space with a default-join channel
    const space = await apiCreateSpace("Open Team", admin.token, {
      is_public: true,
    }, workerConfig.apiConfig);
    const channel = await apiCreateSpaceChannel(
      space.id,
      "welcome",
      admin.token,
      undefined,
      workerConfig.apiConfig,
    );
    await apiUpdateChannel(
      channel.id,
      {
        name: "welcome",
        description: "",
        is_public: true,
        default_role: "write",
        default_join: true,
      },
      admin.token,
      workerConfig.apiConfig,
    );

    // Also create a non-default channel
    await apiCreateSpaceChannel(
      space.id,
      "optional",
      admin.token,
      undefined,
      workerConfig.apiConfig,
    );

    // Regular user joins the space via API
    await apiJoinSpace(space.id, regularUser.token, workerConfig.apiConfig);

    // Login as regular user and verify they can see the welcome channel
    await loginViaToken(page, regularUser.token);

    // Navigate to the space
    await page.getByText("Open Team").first().click();

    // Should see the welcome channel (auto-joined)
    await expect(page.getByText("welcome").first()).toBeVisible({
      timeout: 5_000,
    });

    // Verify via API that user has the default-join channel
    const channels = await apiGetChannels(regularUser.token, workerConfig.apiConfig);
    const channelNames = channels.map((c) => c.name);
    expect(channelNames).toContain("welcome");
    expect(channelNames).not.toContain("optional");
  });

  test("new member auto-joins multiple default channels", async ({
    workerConfig,
  }) => {
    const admin = await setupAdminUser(workerConfig.apiConfig);
    const regularUser = await setupRegularUser("multi", "Multi User", workerConfig.apiConfig);

    const space = await apiCreateSpace("Big Team", admin.token, {
      is_public: true,
    }, workerConfig.apiConfig);

    const channelNames = ["general", "announcements", "random"];
    const channelIds: string[] = [];

    for (const name of channelNames) {
      const ch = await apiCreateSpaceChannel(
        space.id,
        name,
        admin.token,
        undefined,
        workerConfig.apiConfig,
      );
      channelIds.push(ch.id);
      await apiUpdateChannel(
        ch.id,
        {
          name,
          description: "",
          is_public: true,
          default_role: "write",
          default_join: true,
        },
        admin.token,
        workerConfig.apiConfig,
      );
    }

    // Join space
    await apiJoinSpace(space.id, regularUser.token, workerConfig.apiConfig);

    // Verify all three channels appear
    const channels = await apiGetChannels(regularUser.token, workerConfig.apiConfig);
    const userChannelIds = channels.map((c) => c.id);
    for (const id of channelIds) {
      expect(userChannelIds).toContain(id);
    }
  });
});
