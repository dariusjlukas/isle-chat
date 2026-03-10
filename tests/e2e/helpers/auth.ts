/**
 * Playwright helpers for authenticating users via the browser.
 *
 * The app uses PKI (ECDSA P-256) keys stored in IndexedDB and session tokens
 * in localStorage. For most tests we bypass the UI registration flow by:
 * 1. Registering via the API (helpers/api.ts)
 * 2. Injecting the session token into localStorage
 *
 * For tests that specifically test the registration/login UI flow, we drive
 * the actual UI elements.
 */

import { type Page } from "@playwright/test";
import {
  apiRegisterUser,
  apiPasswordRegister,
  apiEnablePasswordAuth,
  apiSetupTotp,
  apiVerifyTotpSetup,
  promoteToOwner,
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
  promoteToOwner(data.userId, config);
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
 * Inject a session token into the page's localStorage and navigate to the app.
 * This bypasses the login UI for faster test setup.
 */
export async function loginViaToken(
  page: Page,
  token: string,
): Promise<void> {
  // Navigate to the app first to set the origin
  await page.goto("/");
  // Inject the session token
  await page.evaluate((t) => {
    localStorage.setItem("session_token", t);
  }, token);
  // Reload to pick up the token
  await page.goto("/");
  // Wait for the app to be loaded - look for the header with "Isle Chat"
  await page.getByText("Isle Chat").first().waitFor({ timeout: 10_000 });
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
