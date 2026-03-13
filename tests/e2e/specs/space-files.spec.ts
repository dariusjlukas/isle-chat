/**
 * E2E tests for the space files feature:
 * - File browser UI (upload, folder creation, rename, delete)
 * - Permissions modal
 * - Version history
 * - Inline preview
 * - Admin storage panel
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
  apiEnableFilesTool,
  apiUploadSpaceFile,
  apiCreateSpaceFolder,
  apiJoinSpace,
  apiAcceptSpaceInvite,
} from "../helpers/api.js";

let admin: TestUser;

test.beforeEach(async ({ workerConfig }) => {
  resetDatabase(workerConfig.dbConfig);
  admin = await setupAdminUser(workerConfig.apiConfig);
});

/** Navigate to a space's file browser by clicking Files in the sidebar. */
async function openFileBrowser(
  page: import("@playwright/test").Page,
  spaceName: string,
) {
  // Wait for sidebar to load and click the space
  await page.getByText(spaceName).first().click({ timeout: 10_000 });
  // Click the Files tool link
  await page.getByRole("button", { name: "Files" }).click();
  // Wait for the file browser header
  await expect(page.getByText(`${spaceName} — Files`)).toBeVisible({
    timeout: 10_000,
  });
}

test.describe("File browser basics", () => {
  test("shows empty state for new space", async ({ page, workerConfig }) => {
    const space = await apiCreateSpace(
      "TestSpace",
      admin.token,
      undefined,
      workerConfig.apiConfig,
    );
    await apiEnableFilesTool(
      space.id,
      admin.token,
      workerConfig.apiConfig,
    );

    await loginViaToken(page, admin.token);
    await openFileBrowser(page, "TestSpace");

    await expect(page.getByText("This folder is empty")).toBeVisible();
  });

  test("can create a folder via UI", async ({ page, workerConfig }) => {
    const space = await apiCreateSpace(
      "TestSpace",
      admin.token,
      undefined,
      workerConfig.apiConfig,
    );
    await apiEnableFilesTool(
      space.id,
      admin.token,
      workerConfig.apiConfig,
    );

    await loginViaToken(page, admin.token);
    await openFileBrowser(page, "TestSpace");

    // Click New Folder
    await page.getByRole("button", { name: "New Folder" }).click();
    await page.getByPlaceholder("Folder name").fill("Documents");
    await page.getByRole("button", { name: "Create" }).click();

    // Folder should appear in the list
    await expect(page.getByText("Documents")).toBeVisible({ timeout: 5_000 });
  });

  test("can navigate into and out of folders", async ({
    page,
    workerConfig,
  }) => {
    const space = await apiCreateSpace(
      "TestSpace",
      admin.token,
      undefined,
      workerConfig.apiConfig,
    );
    await apiEnableFilesTool(
      space.id,
      admin.token,
      workerConfig.apiConfig,
    );
    const folder = await apiCreateSpaceFolder(
      space.id,
      "MyFolder",
      admin.token,
      undefined,
      workerConfig.apiConfig,
    );
    await apiUploadSpaceFile(
      space.id,
      "inner.txt",
      "hello",
      admin.token,
      folder.id,
      workerConfig.apiConfig,
    );

    await loginViaToken(page, admin.token);
    await openFileBrowser(page, "TestSpace");

    // Navigate into folder
    await page.getByText("MyFolder").click();
    await expect(page.getByText("inner.txt")).toBeVisible({ timeout: 5_000 });

    // Navigate back via home breadcrumb
    await page.locator("button svg.fa-house").first().click();
    await expect(page.getByText("MyFolder")).toBeVisible({ timeout: 5_000 });
  });

  test("uploaded files appear in listing", async ({ page, workerConfig }) => {
    const space = await apiCreateSpace(
      "TestSpace",
      admin.token,
      undefined,
      workerConfig.apiConfig,
    );
    await apiEnableFilesTool(
      space.id,
      admin.token,
      workerConfig.apiConfig,
    );
    await apiUploadSpaceFile(
      space.id,
      "readme.md",
      "# Hello",
      admin.token,
      undefined,
      workerConfig.apiConfig,
    );

    await loginViaToken(page, admin.token);
    await openFileBrowser(page, "TestSpace");

    await expect(page.getByText("readme.md")).toBeVisible({ timeout: 5_000 });
  });
});

test.describe("File permissions", () => {
  test("permission badge shows in header", async ({ page, workerConfig }) => {
    const space = await apiCreateSpace(
      "TestSpace",
      admin.token,
      undefined,
      workerConfig.apiConfig,
    );
    await apiEnableFilesTool(
      space.id,
      admin.token,
      workerConfig.apiConfig,
    );

    await loginViaToken(page, admin.token);
    await openFileBrowser(page, "TestSpace");

    // Admin/owner should see "owner" badge
    await expect(page.getByText("owner").first()).toBeVisible();
  });

  test("permissions modal opens and shows auto-granted owner", async ({
    page,
    workerConfig,
  }) => {
    const space = await apiCreateSpace(
      "TestSpace",
      admin.token,
      undefined,
      workerConfig.apiConfig,
    );
    await apiEnableFilesTool(
      space.id,
      admin.token,
      workerConfig.apiConfig,
    );
    await apiUploadSpaceFile(
      space.id,
      "file.txt",
      "content",
      admin.token,
      undefined,
      workerConfig.apiConfig,
    );

    await loginViaToken(page, admin.token);
    await openFileBrowser(page, "TestSpace");

    // Hover to reveal action buttons, click shield icon
    const row = page.locator("text=file.txt").locator("../..");
    await row.hover();
    await row.getByTitle("Permissions").click();

    // Modal should appear with the file name
    await expect(
      page.getByText("Permissions — file.txt"),
    ).toBeVisible({ timeout: 5_000 });

    // Should show the admin user's auto-granted owner permission
    await expect(page.getByText("admin").first()).toBeVisible();
  });

  test("read-only user sees no upload/folder buttons", async ({
    page,
    workerConfig,
  }) => {
    const space = await apiCreateSpace(
      "TestSpace",
      admin.token,
      undefined,
      workerConfig.apiConfig,
    );
    await apiEnableFilesTool(
      space.id,
      admin.token,
      workerConfig.apiConfig,
    );

    const user = await setupRegularUser(
      "viewer",
      "Viewer User",
      workerConfig.apiConfig,
    );
    // Invite as read-only and accept the invite
    const res = await fetch(
      `${workerConfig.apiConfig.apiBase}/api/spaces/${space.id}/members`,
      {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
          Authorization: `Bearer ${admin.token}`,
        },
        body: JSON.stringify({ user_id: user.userId, role: "read" }),
      },
    );
    expect(res.ok).toBeTruthy();
    await apiAcceptSpaceInvite(
      space.id,
      user.token,
      workerConfig.apiConfig,
    );

    await loginViaToken(page, user.token);
    await openFileBrowser(page, "TestSpace");

    // Should show view badge
    await expect(page.getByText("view").first()).toBeVisible();
    // Upload and New Folder buttons should not exist
    await expect(
      page.getByRole("button", { name: "Upload" }),
    ).not.toBeVisible();
    await expect(
      page.getByRole("button", { name: "New Folder" }),
    ).not.toBeVisible();
  });
});

test.describe("Version history", () => {
  test("version modal shows initial version", async ({
    page,
    workerConfig,
  }) => {
    const space = await apiCreateSpace(
      "TestSpace",
      admin.token,
      undefined,
      workerConfig.apiConfig,
    );
    await apiEnableFilesTool(
      space.id,
      admin.token,
      workerConfig.apiConfig,
    );
    await apiUploadSpaceFile(
      space.id,
      "versioned.txt",
      "v1 content",
      admin.token,
      undefined,
      workerConfig.apiConfig,
    );

    await loginViaToken(page, admin.token);
    await openFileBrowser(page, "TestSpace");

    // Open version history
    const row = page.locator("text=versioned.txt").locator("../..");
    await row.hover();
    await row.getByTitle("Versions").click();

    await expect(
      page.getByText("Version History — versioned.txt"),
    ).toBeVisible({ timeout: 5_000 });
    await expect(page.getByText("v1")).toBeVisible();
    await expect(page.getByText("current")).toBeVisible();
  });
});

test.describe("Inline preview", () => {
  test("text file preview shows content", async ({ page, workerConfig }) => {
    const space = await apiCreateSpace(
      "TestSpace",
      admin.token,
      undefined,
      workerConfig.apiConfig,
    );
    await apiEnableFilesTool(
      space.id,
      admin.token,
      workerConfig.apiConfig,
    );
    await apiUploadSpaceFile(
      space.id,
      "hello.txt",
      "Hello from the preview!",
      admin.token,
      undefined,
      workerConfig.apiConfig,
    );

    await loginViaToken(page, admin.token);
    await openFileBrowser(page, "TestSpace");

    // Click the filename to open preview
    await page.getByText("hello.txt").click();

    // Preview modal should show the file content
    await expect(page.getByText("Hello from the preview!")).toBeVisible({
      timeout: 10_000,
    });
  });
});

test.describe("Admin storage panel", () => {
  test("storage tab visible in admin panel", async ({
    page,
    workerConfig,
  }) => {
    const space = await apiCreateSpace(
      "TestSpace",
      admin.token,
      undefined,
      workerConfig.apiConfig,
    );
    await apiEnableFilesTool(
      space.id,
      admin.token,
      workerConfig.apiConfig,
    );
    await apiUploadSpaceFile(
      space.id,
      "data.bin",
      "x".repeat(1000),
      admin.token,
      undefined,
      workerConfig.apiConfig,
    );

    await loginViaToken(page, admin.token);

    // Open admin panel
    const avatarBtn = page.locator(
      "header .flex.items-center.justify-end button.rounded-full",
    );
    await avatarBtn.click();
    await page.getByRole("menuitem", { name: "Admin Panel" }).click();

    await expect(page.getByText("Admin Panel").first()).toBeVisible({
      timeout: 10_000,
    });

    // Click Storage tab
    await page.getByRole("button", { name: "Storage" }).click();

    // Should show total storage used
    await expect(page.getByText("Total Storage Used")).toBeVisible({
      timeout: 5_000,
    });
    // Should show the space name in the breakdown (scoped to admin panel)
    await expect(
      page.getByLabel("Admin Panel").getByText("TestSpace"),
    ).toBeVisible();
  });
});
