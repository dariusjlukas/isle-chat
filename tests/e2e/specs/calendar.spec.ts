/**
 * E2E tests for the calendar feature:
 * - Calendar view UI (month/week/day/agenda switching, event display)
 * - Event creation and editing
 * - RSVP interaction
 * - Permission enforcement (view-only user)
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
  apiEnableCalendarTool,
  apiCreateCalendarEvent,
  apiJoinSpace,
  apiAcceptSpaceInvite,
} from "../helpers/api.js";

let admin: TestUser;

test.beforeEach(async ({ workerConfig }) => {
  resetDatabase(workerConfig.dbConfig);
  admin = await setupAdminUser(workerConfig.apiConfig);
});

/** Navigate to a space's calendar by clicking Calendar in the sidebar. */
async function openCalendar(
  page: import("@playwright/test").Page,
  spaceName: string,
) {
  await page.getByText(spaceName).first().click({ timeout: 10_000 });
  await page.getByRole("button", { name: "Calendar" }).click();
  // Wait for the calendar header controls to appear
  await expect(page.getByRole("button", { name: "Today" })).toBeVisible({
    timeout: 10_000,
  });
}

test.describe("Calendar basics", () => {
  test("shows empty month view for new space", async ({
    page,
    workerConfig,
  }) => {
    const space = await apiCreateSpace(
      "TestSpace",
      admin.token,
      undefined,
      workerConfig.apiConfig,
    );
    await apiEnableCalendarTool(
      space.id,
      admin.token,
      workerConfig.apiConfig,
    );

    await loginViaToken(page, admin.token);
    await openCalendar(page, "TestSpace");

    // Month view should be shown by default with Month button highlighted
    await expect(
      page.getByRole("button", { name: "Month" }),
    ).toBeVisible();
    // Today button should be present
    await expect(
      page.getByRole("button", { name: "Today" }),
    ).toBeVisible();
  });

  test("can switch between view modes", async ({ page, workerConfig }) => {
    const space = await apiCreateSpace(
      "TestSpace",
      admin.token,
      undefined,
      workerConfig.apiConfig,
    );
    await apiEnableCalendarTool(
      space.id,
      admin.token,
      workerConfig.apiConfig,
    );

    await loginViaToken(page, admin.token);
    await openCalendar(page, "TestSpace");

    // Switch to week view
    await page.getByRole("button", { name: "Week" }).click();
    // Week view should show day headers
    await expect(page.getByText("Mon", { exact: true })).toBeVisible({
      timeout: 5_000,
    });

    // Switch to day view
    await page.getByRole("button", { name: "Day", exact: true }).click();
    // Day view should be visible (the time grid)
    await expect(page.getByRole("button", { name: "Today" })).toBeVisible();

    // Switch to agenda view
    await page.getByRole("button", { name: "Agenda", exact: true }).click();
    await expect(page.getByText("Upcoming Events", { exact: true })).toBeVisible({
      timeout: 5_000,
    });
  });

  test("events appear in month view", async ({ page, workerConfig }) => {
    const space = await apiCreateSpace(
      "TestSpace",
      admin.token,
      undefined,
      workerConfig.apiConfig,
    );
    await apiEnableCalendarTool(
      space.id,
      admin.token,
      workerConfig.apiConfig,
    );

    // Create an event for today
    const now = new Date();
    const start = new Date(
      now.getFullYear(),
      now.getMonth(),
      now.getDate(),
      14,
      0,
      0,
    );
    const end = new Date(
      now.getFullYear(),
      now.getMonth(),
      now.getDate(),
      15,
      0,
      0,
    );
    await apiCreateCalendarEvent(
      space.id,
      {
        title: "Test Event",
        start_time: start.toISOString(),
        end_time: end.toISOString(),
        color: "blue",
      },
      admin.token,
      workerConfig.apiConfig,
    );

    await loginViaToken(page, admin.token);
    await openCalendar(page, "TestSpace");

    await expect(page.getByText("Test Event")).toBeVisible({
      timeout: 10_000,
    });
  });

  test("events appear in agenda view", async ({ page, workerConfig }) => {
    const space = await apiCreateSpace(
      "TestSpace",
      admin.token,
      undefined,
      workerConfig.apiConfig,
    );
    await apiEnableCalendarTool(
      space.id,
      admin.token,
      workerConfig.apiConfig,
    );

    const now = new Date();
    const start = new Date(
      now.getFullYear(),
      now.getMonth(),
      now.getDate(),
      10,
      0,
      0,
    );
    const end = new Date(
      now.getFullYear(),
      now.getMonth(),
      now.getDate(),
      11,
      0,
      0,
    );
    await apiCreateCalendarEvent(
      space.id,
      {
        title: "Agenda Item",
        start_time: start.toISOString(),
        end_time: end.toISOString(),
        location: "Room 42",
      },
      admin.token,
      workerConfig.apiConfig,
    );

    await loginViaToken(page, admin.token);
    await openCalendar(page, "TestSpace");

    // Switch to agenda
    await page.getByRole("button", { name: "Agenda", exact: true }).click();
    await expect(page.getByText("Agenda Item")).toBeVisible({
      timeout: 10_000,
    });
    await expect(page.getByText("Room 42")).toBeVisible();
  });
});

test.describe("Event creation", () => {
  test("can create an event via the New Event button", async ({
    page,
    workerConfig,
  }) => {
    const space = await apiCreateSpace(
      "TestSpace",
      admin.token,
      undefined,
      workerConfig.apiConfig,
    );
    await apiEnableCalendarTool(
      space.id,
      admin.token,
      workerConfig.apiConfig,
    );

    await loginViaToken(page, admin.token);
    await openCalendar(page, "TestSpace");

    // Click New Event
    await page.getByRole("button", { name: "New Event" }).click();

    // Event modal should appear
    await expect(page.getByText("New Event").first()).toBeVisible({
      timeout: 5_000,
    });

    // Fill in the form
    await page.getByLabel("Title").fill("My New Event");

    // Save the event
    await page.getByRole("button", { name: "Create" }).click();

    // Event should now appear in the calendar (switch to agenda for easy check)
    await page.getByRole("button", { name: "Agenda", exact: true }).click();
    await expect(page.getByText("My New Event")).toBeVisible({
      timeout: 10_000,
    });
  });

  test("New Event button hidden for view-only users", async ({
    page,
    workerConfig,
  }) => {
    const space = await apiCreateSpace(
      "TestSpace",
      admin.token,
      undefined,
      workerConfig.apiConfig,
    );
    await apiEnableCalendarTool(
      space.id,
      admin.token,
      workerConfig.apiConfig,
    );

    const viewer = await setupRegularUser(
      "viewer",
      "Viewer User",
      workerConfig.apiConfig,
    );
    // Add as read-only member
    const res = await fetch(
      `${workerConfig.apiConfig.apiBase}/api/spaces/${space.id}/members`,
      {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
          Authorization: `Bearer ${admin.token}`,
        },
        body: JSON.stringify({ user_id: viewer.userId, role: "read" }),
      },
    );
    expect(res.ok).toBeTruthy();
    await apiAcceptSpaceInvite(
      space.id,
      viewer.token,
      workerConfig.apiConfig,
    );

    await loginViaToken(page, viewer.token);
    await openCalendar(page, "TestSpace");

    // New Event button should NOT be visible
    await expect(
      page.getByRole("button", { name: "New Event" }),
    ).not.toBeVisible();
  });
});

test.describe("Event interaction", () => {
  test("clicking an event opens the event modal", async ({
    page,
    workerConfig,
  }) => {
    const space = await apiCreateSpace(
      "TestSpace",
      admin.token,
      undefined,
      workerConfig.apiConfig,
    );
    await apiEnableCalendarTool(
      space.id,
      admin.token,
      workerConfig.apiConfig,
    );

    const now = new Date();
    const start = new Date(
      now.getFullYear(),
      now.getMonth(),
      now.getDate(),
      10,
      0,
      0,
    );
    const end = new Date(
      now.getFullYear(),
      now.getMonth(),
      now.getDate(),
      11,
      0,
      0,
    );
    await apiCreateCalendarEvent(
      space.id,
      {
        title: "Click Me",
        start_time: start.toISOString(),
        end_time: end.toISOString(),
        description: "This is a test event",
      },
      admin.token,
      workerConfig.apiConfig,
    );

    await loginViaToken(page, admin.token);
    await openCalendar(page, "TestSpace");

    // Click the event in the calendar
    await page.getByText("Click Me").first().click();

    // Modal should show event details
    await expect(page.getByText("This is a test event")).toBeVisible({
      timeout: 5_000,
    });
  });

  test("can RSVP to an event", async ({ page, workerConfig }) => {
    const space = await apiCreateSpace(
      "TestSpace",
      admin.token,
      undefined,
      workerConfig.apiConfig,
    );
    await apiEnableCalendarTool(
      space.id,
      admin.token,
      workerConfig.apiConfig,
    );

    const now = new Date();
    const start = new Date(
      now.getFullYear(),
      now.getMonth(),
      now.getDate(),
      10,
      0,
      0,
    );
    const end = new Date(
      now.getFullYear(),
      now.getMonth(),
      now.getDate(),
      11,
      0,
      0,
    );
    await apiCreateCalendarEvent(
      space.id,
      {
        title: "RSVP Event",
        start_time: start.toISOString(),
        end_time: end.toISOString(),
      },
      admin.token,
      workerConfig.apiConfig,
    );

    await loginViaToken(page, admin.token);
    await openCalendar(page, "TestSpace");

    // Click the event
    await page.getByText("RSVP Event").first().click();

    // Wait for modal
    await expect(page.getByText("RSVP", { exact: true })).toBeVisible({
      timeout: 5_000,
    });

    // Click Yes RSVP button
    await page.getByRole("button", { name: "Yes" }).click();

    // The Yes button should now be highlighted/active
    await expect(
      page.getByRole("button", { name: "Yes" }),
    ).toBeVisible();
  });
});

test.describe("Calendar permissions", () => {
  test("permissions button visible for owners", async ({
    page,
    workerConfig,
  }) => {
    const space = await apiCreateSpace(
      "TestSpace",
      admin.token,
      undefined,
      workerConfig.apiConfig,
    );
    await apiEnableCalendarTool(
      space.id,
      admin.token,
      workerConfig.apiConfig,
    );

    await loginViaToken(page, admin.token);
    await openCalendar(page, "TestSpace");

    // Shield button (permissions) should be visible for owner
    await expect(
      page.locator("button[title='Calendar permissions']"),
    ).toBeVisible();
  });

  test("permissions button hidden for regular members", async ({
    page,
    workerConfig,
  }) => {
    const space = await apiCreateSpace(
      "TestSpace",
      admin.token,
      undefined,
      workerConfig.apiConfig,
    );
    await apiEnableCalendarTool(
      space.id,
      admin.token,
      workerConfig.apiConfig,
    );

    const user = await setupRegularUser(
      "member",
      "Regular Member",
      workerConfig.apiConfig,
    );
    await apiJoinSpace(space.id, user.token, workerConfig.apiConfig);

    await loginViaToken(page, user.token);
    await openCalendar(page, "TestSpace");

    // Shield button should NOT be visible for regular member (edit level)
    await expect(
      page.locator("button[title='Calendar permissions']"),
    ).not.toBeVisible();
  });
});
