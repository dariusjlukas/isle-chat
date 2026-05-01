/**
 * Playwright helpers for authenticating users via the browser.
 *
 * Since P1.4 Release C the app uses cookie-based auth (HttpOnly `session`
 * cookie + non-HttpOnly `csrf` cookie). For most tests we bypass the UI
 * registration flow by:
 * 1. Registering via the API (helpers/api.ts) — captures session cookie
 *    from the Set-Cookie header
 * 2. Injecting both cookies into the Playwright BrowserContext jar so the
 *    browser sends them on its next navigation
 *
 * For tests that specifically test the registration/login UI flow, we drive
 * the actual UI elements.
 */

import { type Page, type BrowserContext } from "@playwright/test";
import {
  apiRegisterUser,
  apiPasswordRegister,
  apiEnablePasswordAuth,
  apiSetupTotp,
  apiVerifyTotpSetup,
  setRegistrationOpen,
  completeSetup,
  type ApiConfig,
} from "./api.js";
import { generateTotpCode } from "./totp.js";

export interface TestUser {
  token: string;
  userId: string;
  username: string;
  recoveryKeys: string[];
}

export interface PasswordTestUser {
  token: string;
  userId: string;
  username: string;
  password: string;
  totpSecret?: string;
}

/**
 * Set up an admin/owner user via the API and configure open registration.
 * Returns the user info including auth token.
 */
export async function setupAdminUser(
  config?: ApiConfig,
): Promise<TestUser> {
  const data = await apiRegisterUser("admin", "Admin User", config);
  // First user automatically gets "owner" role
  await setRegistrationOpen(data.token, config);
  await completeSetup(data.token, config);
  return {
    token: data.token,
    userId: data.userId,
    username: "admin",
    recoveryKeys: data.recoveryKeys,
  };
}

/**
 * Register a regular user via the API.
 */
export async function setupRegularUser(
  username: string = "testuser",
  displayName: string = "Test User",
  config?: ApiConfig,
): Promise<TestUser> {
  const data = await apiRegisterUser(username, displayName, config);
  return {
    token: data.token,
    userId: data.userId,
    username,
    recoveryKeys: data.recoveryKeys,
  };
}

/**
 * P1.4 Release C: inject the session and csrf cookies into the browser
 * context's cookie jar and navigate to the app. The session value here is
 * the same value the server set on the original Set-Cookie response — i.e.
 * the `token` field returned by `setupAdminUser` / `setupRegularUser` /
 * `setupPasswordUser`. The csrf cookie value is matched by the api request
 * wrapper's X-CSRF-Token header so the frontend's state-changing requests
 * succeed.
 */
export async function loginViaToken(
  page: Page,
  token: string,
): Promise<void> {
  const context: BrowserContext = page.context();
  // Resolve the origin from the page's existing target URL (set in playwright
  // config) — derive host from the apiBase which test runners point to.
  const apiBase =
    process.env.TEST_API_BASE ?? `http://localhost:${process.env.TEST_FRONTEND_PORT ?? "5173"}`;
  const url = new URL(apiBase);
  await context.addCookies([
    {
      name: "session",
      value: token,
      domain: url.hostname,
      path: "/",
      httpOnly: true,
      sameSite: "Strict",
    },
    {
      name: "csrf",
      value: "e2e-csrf",
      domain: url.hostname,
      path: "/",
      httpOnly: false,
      sameSite: "Strict",
    },
  ]);
  // Set the non-sensitive logged_in flag the frontend uses as a bootstrap probe.
  await page.goto("/");
  await page.evaluate(() => localStorage.setItem("logged_in", "1"));
  await page.goto("/");
  // Wait for the app to be loaded - look for the header with "EnclaveStation"
  await page.getByText("EnclaveStation").first().waitFor({ timeout: 10_000 });
}

/**
 * Set up a password user (requires admin to have enabled password auth first).
 * Optionally sets up TOTP.
 */
export async function setupPasswordUser(
  username: string,
  password: string,
  options?: { enableTotp?: boolean },
  config?: ApiConfig,
): Promise<PasswordTestUser> {
  const data = await apiPasswordRegister(username, `${username} User`, password, config);
  let totpSecret: string | undefined;
  if (options?.enableTotp) {
    totpSecret = await apiSetupTotp(data.token, config);
    const code = generateTotpCode(totpSecret);
    await apiVerifyTotpSetup(code, data.token, config);
  }
  return {
    token: data.token,
    userId: data.userId,
    username,
    password,
    totpSecret,
  };
}
