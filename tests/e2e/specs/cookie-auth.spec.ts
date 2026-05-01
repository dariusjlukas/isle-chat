/**
 * E2E tests for the cookie-only auth flow shipped in P1.4 Release C (A.1).
 *
 * The backend issues an HttpOnly `session` cookie (the actual session token)
 * plus a non-HttpOnly `csrf` cookie (read by JS for the X-CSRF-Token
 * double-submit header). Bearer tokens and `?token=` query params have been
 * removed. State-changing requests must carry a matching X-CSRF-Token header.
 *
 * API-level coverage lives in tests/api/test_cookie_auth.py. These browser
 * tests verify the integration end-to-end:
 *   - cookies have the right attributes after login
 *   - localStorage holds only the non-sensitive `logged_in` probe (no token)
 *   - reload preserves auth via cookies
 *   - logout clears cookies
 *   - WS upgrade does not include `?token=` (cookie-only)
 */

import type { Page, BrowserContext } from "@playwright/test";
import { test, expect } from "../fixtures.js";
import { resetDatabase } from "../helpers/db.js";

/**
 * Register the very first user via the UI (PKI / Browser Key flow). This is
 * the most realistic cookie-auth path: the browser receives Set-Cookie
 * headers from /api/auth/pki/register and stores them in its cookie jar.
 *
 * We deliberately do NOT use helpers/auth.ts loginViaToken here — that
 * helper predates Release C and works by injecting a session_token into
 * localStorage, which the cookie-only backend ignores.
 */
async function registerFirstUserViaUi(
  page: Page,
  username: string = "cookieuser",
): Promise<void> {
  await page.goto("/");
  await page.getByText("Create an account").click();
  await page.getByPlaceholder("johndoe").fill(username);
  await page.getByPlaceholder("John Doe").fill("Cookie User");
  await page.getByText("Browser Key").click();
  await page.getByLabel("Browser Key PIN").fill("testpin1234");
  await page.getByLabel("Confirm PIN").fill("testpin1234");
  await page
    .getByRole("button", { name: /create account with browser key/i })
    .click();
  // Recovery keys modal — acknowledge and continue
  await expect(
    page.getByText("Recovery Keys", { exact: true }),
  ).toBeVisible({ timeout: 10_000 });
  await page
    .getByRole("checkbox", { name: /I have saved these recovery keys/i })
    .check({ force: true });
  await page.getByRole("button", { name: "Continue" }).click();
  // Wait for the authenticated layout to render
  await expect(page.getByText("EnclaveStation").first()).toBeVisible({
    timeout: 10_000,
  });
  // First user triggers the Setup Wizard — dismiss if present so we land on
  // the normal authenticated UI.
  const setupWizard = page.getByText("Welcome! Server Setup");
  if (await setupWizard.isVisible().catch(() => false)) {
    await page.getByRole("button", { name: "Complete Setup" }).click();
    await setupWizard.waitFor({ state: "hidden", timeout: 5_000 });
  }
}

async function logoutViaUi(page: Page): Promise<void> {
  // Pattern lifted from auth.spec.ts: open avatar dropdown, click Logout.
  const avatarBtn = page.locator(
    "header .flex.items-center.justify-end button.rounded-full",
  );
  await avatarBtn.click();
  await page.getByRole("menuitem", { name: "Logout" }).click();
  // Wait for the login screen to come back — the sign-in button is the
  // canonical "logged out" marker used by other specs.
  await expect(
    page.getByRole("button", { name: /sign in with/i }),
  ).toBeVisible({ timeout: 10_000 });
}

/**
 * Find a cookie by name across the context's cookie jar, scoped to the page
 * URL (so we don't pick up any leftover cookies from another worker).
 */
async function findCookie(
  context: BrowserContext,
  page: Page,
  name: string,
) {
  const cookies = await context.cookies(page.url());
  return cookies.find((c) => c.name === name);
}

test.describe("Cookie auth (P1.4 Release C)", () => {
  test.beforeEach(({ workerConfig }) => {
    resetDatabase(workerConfig.dbConfig);
  });

  test("login sets HttpOnly session cookie and non-HttpOnly csrf cookie", async ({
    page,
    context,
  }) => {
    await registerFirstUserViaUi(page);

    const session = await findCookie(context, page, "session");
    const csrf = await findCookie(context, page, "csrf");

    expect(session, "session cookie should exist after login").toBeDefined();
    expect(csrf, "csrf cookie should exist after login").toBeDefined();
    // session cookie is HttpOnly so JS cannot read it
    expect(session!.httpOnly).toBe(true);
    expect(session!.sameSite).toBe("Strict");
    // csrf cookie must be readable by JS so the request wrapper can attach
    // X-CSRF-Token to state-changing requests (double-submit pattern)
    expect(csrf!.httpOnly).toBe(false);
    expect(csrf!.sameSite).toBe("Strict");
  });

  test("localStorage does not contain session_token", async ({ page }) => {
    await registerFirstUserViaUi(page);
    const sessionToken = await page.evaluate(() =>
      localStorage.getItem("session_token"),
    );
    expect(sessionToken).toBeNull();
  });

  test("logged_in flag is set in localStorage as a non-sensitive auth probe", async ({
    page,
  }) => {
    await registerFirstUserViaUi(page);
    const flag = await page.evaluate(() => localStorage.getItem("logged_in"));
    expect(flag).toBe("1");
  });

  test("document.cookie exposes csrf but not session", async ({ page }) => {
    await registerFirstUserViaUi(page);
    const docCookie = await page.evaluate(() => document.cookie);
    // session is HttpOnly, must not appear here
    expect(docCookie).not.toContain("session=");
    // csrf is intentionally readable (frontend needs it for X-CSRF-Token)
    expect(docCookie).toContain("csrf=");
  });

  test("reload preserves auth via cookies", async ({ page }) => {
    await registerFirstUserViaUi(page);
    // Sanity: we're on the authenticated layout
    await expect(page.getByText("EnclaveStation").first()).toBeVisible();
    // Reload — cookies persist, the app should still be authenticated.
    // Login page is only shown when isAuthenticated is false, so the
    // EnclaveStation header still being there is the auth signal.
    await page.reload();
    await expect(page.getByText("EnclaveStation").first()).toBeVisible({
      timeout: 10_000,
    });
    // Login button MUST NOT be visible (would mean we got bounced to login)
    await expect(
      page.getByRole("button", { name: /sign in with/i }),
    ).toHaveCount(0);
  });

  test("logout clears session and csrf cookies", async ({ page, context }) => {
    await registerFirstUserViaUi(page);

    // Pre-condition: cookies exist
    expect(await findCookie(context, page, "session")).toBeDefined();
    expect(await findCookie(context, page, "csrf")).toBeDefined();

    await logoutViaUi(page);

    // Logout response sends Set-Cookie with Max-Age=0 for both cookies; the
    // browser then drops them from the jar.
    const session = await findCookie(context, page, "session");
    const csrf = await findCookie(context, page, "csrf");
    expect(session, "session cookie should be cleared on logout").toBeUndefined();
    expect(csrf, "csrf cookie should be cleared on logout").toBeUndefined();
  });

  test("WebSocket connects without ?token= query param", async ({ page }) => {
    const wsUrls: string[] = [];
    page.on("websocket", (ws) => wsUrls.push(ws.url()));

    await registerFirstUserViaUi(page);

    // The app opens a WS shortly after the authenticated layout renders.
    // Wait for at least one WS to be observed, then verify its URL.
    await expect.poll(() => wsUrls.length, { timeout: 10_000 }).toBeGreaterThan(0);

    for (const url of wsUrls) {
      expect(url, "WS URL should not contain ?token=").not.toContain("?token=");
      expect(url, "WS URL should not contain &token=").not.toContain("&token=");
    }
  });
});
