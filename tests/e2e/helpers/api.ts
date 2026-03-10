/**
 * Direct API helpers for E2E test setup.
 * Used to set up preconditions (create users, set settings) without going through the UI.
 *
 * All functions accept an optional ApiConfig for per-worker isolation.
 * If not provided, they fall back to env-var defaults (single-worker mode).
 */

import { execSync } from "child_process";

export interface ApiConfig {
  apiBase: string;
  pgUser: string;
  pgDb: string;
  pgContainer: string;
}

const defaultConfig: ApiConfig = {
  apiBase: `http://localhost:${process.env.TEST_BACKEND_PORT ?? "9099"}`,
  pgUser: process.env.POSTGRES_USER ?? "chatapp_test",
  pgDb: process.env.POSTGRES_DB ?? "chatapp_test",
  pgContainer: process.env.TEST_PG_CONTAINER ?? "chatapp-test-postgres",
};

function apiPost(
  path: string,
  body: unknown,
  token?: string,
  config: ApiConfig = defaultConfig,
): Promise<Response> {
  const headers: Record<string, string> = {
    "Content-Type": "application/json",
  };
  if (token) headers["Authorization"] = `Bearer ${token}`;
  return fetch(`${config.apiBase}${path}`, {
    method: "POST",
    headers,
    body: JSON.stringify(body),
  });
}

function apiPut(
  path: string,
  body: unknown,
  token?: string,
  config: ApiConfig = defaultConfig,
): Promise<Response> {
  const headers: Record<string, string> = {
    "Content-Type": "application/json",
  };
  if (token) headers["Authorization"] = `Bearer ${token}`;
  return fetch(`${config.apiBase}${path}`, {
    method: "PUT",
    headers,
    body: JSON.stringify(body),
  });
}

function apiGet(
  path: string,
  token?: string,
  config: ApiConfig = defaultConfig,
): Promise<Response> {
  const headers: Record<string, string> = {};
  if (token) headers["Authorization"] = `Bearer ${token}`;
  return fetch(`${config.apiBase}${path}`, { headers });
}

/**
 * Register a user via the API using PKI auth.
 */
export async function apiRegisterUser(
  username: string,
  displayName: string,
  config: ApiConfig = defaultConfig,
): Promise<{ token: string; userId: string; recoveryKeys: string[] }> {
  const { generateKeyPairSync, createSign } = await import("crypto");
  const { privateKey, publicKey } = generateKeyPairSync("ec", {
    namedCurve: "prime256v1",
  });

  const spkiDer = publicKey.export({ type: "spki", format: "der" });
  const pubKeyB64url = spkiDer
    .toString("base64")
    .replace(/\+/g, "-")
    .replace(/\//g, "_")
    .replace(/=+$/, "");

  const challengeRes = await apiPost(
    "/api/auth/pki/challenge",
    {},
    undefined,
    config,
  );
  const { challenge } = (await challengeRes.json()) as { challenge: string };

  const signer = createSign("SHA256");
  signer.update(challenge);
  const derSig = signer.sign(privateKey);

  let offset = 2;
  offset += 1;
  const rLen = derSig[offset++];
  const rBytes = derSig.subarray(offset, offset + rLen);
  offset += rLen;
  offset += 1;
  const sLen = derSig[offset++];
  const sBytes = derSig.subarray(offset, offset + sLen);

  const r = Buffer.alloc(32);
  const s = Buffer.alloc(32);
  rBytes
    .subarray(rBytes.length > 32 ? rBytes.length - 32 : 0)
    .copy(r, Math.max(0, 32 - rBytes.length));
  sBytes
    .subarray(sBytes.length > 32 ? sBytes.length - 32 : 0)
    .copy(s, Math.max(0, 32 - sBytes.length));
  const rawSig = Buffer.concat([r, s]);
  const sigB64url = rawSig
    .toString("base64")
    .replace(/\+/g, "-")
    .replace(/\//g, "_")
    .replace(/=+$/, "");

  const regRes = await apiPost(
    "/api/auth/pki/register",
    {
      username,
      display_name: displayName,
      public_key: pubKeyB64url,
      challenge,
      signature: sigB64url,
    },
    undefined,
    config,
  );
  const data = (await regRes.json()) as {
    token: string;
    user: { id: string };
    recovery_keys: string[];
  };
  return {
    token: data.token,
    userId: data.user.id,
    recoveryKeys: data.recovery_keys,
  };
}

/**
 * Promote a user to "owner" role via direct DB update.
 */
export function promoteToOwner(
  userId: string,
  config: ApiConfig = defaultConfig,
): void {
  const sql = `UPDATE users SET role = 'owner' WHERE id = '${userId}'`;
  execSync(
    `docker exec ${config.pgContainer} psql -U "${config.pgUser}" -d "${config.pgDb}" -c "${sql}"`,
    { stdio: "pipe" },
  );
}

/**
 * Set registration mode to "open" so new users can register freely.
 */
export async function setRegistrationOpen(
  token: string,
  config: ApiConfig = defaultConfig,
): Promise<void> {
  await apiPut(
    "/api/admin/settings",
    { registration_mode: "open" },
    token,
    config,
  );
}

/**
 * Complete the server setup wizard so it doesn't appear on login.
 */
export async function completeSetup(
  token: string,
  config: ApiConfig = defaultConfig,
): Promise<void> {
  await apiPost(
    "/api/admin/setup",
    { registration_mode: "open" },
    token,
    config,
  );
}

/**
 * Change a user's server role via the admin API.
 */
export async function apiChangeUserRole(
  userId: string,
  role: string,
  token: string,
  config: ApiConfig = defaultConfig,
): Promise<Response> {
  return apiPut(
    `/api/admin/users/${userId}/role`,
    { role },
    token,
    config,
  );
}

/**
 * Get admin users list via the API.
 */
export async function apiGetAdminUsers(
  token: string,
  config: ApiConfig = defaultConfig,
): Promise<Array<{ id: string; username: string; role: string }>> {
  const res = await apiGet("/api/admin/users", token, config);
  return (await res.json()) as Array<{
    id: string;
    username: string;
    role: string;
  }>;
}

/**
 * Create a standalone channel via the API.
 */
export async function apiCreateChannel(
  name: string,
  token: string,
  options?: { is_public?: boolean; description?: string },
  config: ApiConfig = defaultConfig,
): Promise<{ id: string; name: string }> {
  const res = await apiPost(
    "/api/channels",
    { name, ...options },
    token,
    config,
  );
  return (await res.json()) as { id: string; name: string };
}

/**
 * Create a channel within a space via the API.
 */
export async function apiCreateSpaceChannel(
  spaceId: string,
  name: string,
  token: string,
  options?: { is_public?: boolean; description?: string },
  config: ApiConfig = defaultConfig,
): Promise<{ id: string; name: string }> {
  const res = await apiPost(
    `/api/spaces/${spaceId}/channels`,
    { name, ...options },
    token,
    config,
  );
  return (await res.json()) as { id: string; name: string };
}

/**
 * Create a space via the API.
 */
export async function apiCreateSpace(
  name: string,
  token: string,
  options?: { is_public?: boolean; description?: string },
  config: ApiConfig = defaultConfig,
): Promise<{ id: string; name: string }> {
  const res = await apiPost(
    "/api/spaces",
    { name, ...options },
    token,
    config,
  );
  return (await res.json()) as { id: string; name: string };
}

/**
 * Send a message to a channel via the API.
 */
export async function apiSendMessage(
  channelId: string,
  content: string,
  token: string,
  config: ApiConfig = defaultConfig,
): Promise<{ id: string }> {
  const res = await apiPost(
    `/api/channels/${channelId}/messages`,
    { content },
    token,
    config,
  );
  if (!res.ok) {
    const text = await res.text();
    throw new Error(
      `apiSendMessage failed (${res.status}): ${text.slice(0, 200)}`,
    );
  }
  return (await res.json()) as { id: string };
}

/**
 * Join a public space via the API.
 */
export async function apiJoinSpace(
  spaceId: string,
  token: string,
  config: ApiConfig = defaultConfig,
): Promise<void> {
  const res = await apiPost(
    `/api/spaces/${spaceId}/join`,
    {},
    token,
    config,
  );
  if (!res.ok) {
    const text = await res.text();
    throw new Error(
      `apiJoinSpace failed (${res.status}): ${text.slice(0, 200)}`,
    );
  }
}

/**
 * Join a public channel via the API.
 */
export async function apiJoinChannel(
  channelId: string,
  token: string,
  config: ApiConfig = defaultConfig,
): Promise<void> {
  const res = await apiPost(
    `/api/channels/${channelId}/join`,
    {},
    token,
    config,
  );
  if (!res.ok) {
    const text = await res.text();
    throw new Error(
      `apiJoinChannel failed (${res.status}): ${text.slice(0, 200)}`,
    );
  }
}

/**
 * Get channels list via the API.
 */
export async function apiGetChannels(
  token: string,
  config: ApiConfig = defaultConfig,
): Promise<Array<{ id: string; name: string }>> {
  const res = await apiGet("/api/channels", token, config);
  return (await res.json()) as Array<{ id: string; name: string }>;
}

/**
 * Enable password auth via admin settings.
 */
export async function apiEnablePasswordAuth(
  token: string,
  config: ApiConfig = defaultConfig,
): Promise<void> {
  await apiPut(
    "/api/admin/settings",
    { auth_methods: ["passkey", "pki", "password"] },
    token,
    config,
  );
}

/**
 * Register a user with password auth.
 */
export async function apiPasswordRegister(
  username: string,
  displayName: string,
  password: string,
  config: ApiConfig = defaultConfig,
): Promise<{ token: string; userId: string }> {
  const res = await apiPost(
    "/api/auth/password/register",
    { username, display_name: displayName, password },
    undefined,
    config,
  );
  if (!res.ok) {
    const text = await res.text();
    throw new Error(`Password register failed (${res.status}): ${text}`);
  }
  const data = (await res.json()) as { token: string; user: { id: string } };
  return { token: data.token, userId: data.user.id };
}

/**
 * Login via password auth.
 */
export async function apiPasswordLogin(
  username: string,
  password: string,
  config: ApiConfig = defaultConfig,
): Promise<{ token?: string; mfa_required?: boolean; mfa_token?: string; must_setup_totp?: boolean }> {
  const res = await apiPost(
    "/api/auth/password/login",
    { username, password },
    undefined,
    config,
  );
  if (!res.ok) {
    const text = await res.text();
    throw new Error(`Password login failed (${res.status}): ${text}`);
  }
  return (await res.json()) as { token?: string; mfa_required?: boolean; mfa_token?: string; must_setup_totp?: boolean };
}

/**
 * Set up TOTP for a user and return the secret. Initiates setup and verifies with a valid code.
 */
export async function apiSetupTotp(
  token: string,
  config: ApiConfig = defaultConfig,
): Promise<string> {
  // We need to dynamically import a TOTP library — use the OTPAuth npm package or compute manually.
  // For simplicity, we'll just return the secret and let tests generate codes.
  const res = await apiPost("/api/users/me/totp/setup", {}, token, config);
  if (!res.ok) {
    const text = await res.text();
    throw new Error(`TOTP setup failed (${res.status}): ${text}`);
  }
  const data = (await res.json()) as { secret: string; uri: string };
  return data.secret;
}

/**
 * Verify TOTP setup with a code.
 */
export async function apiVerifyTotpSetup(
  code: string,
  token: string,
  config: ApiConfig = defaultConfig,
): Promise<void> {
  const res = await apiPost(
    "/api/users/me/totp/verify",
    { code },
    token,
    config,
  );
  if (!res.ok) {
    const text = await res.text();
    throw new Error(`TOTP verify failed (${res.status}): ${text}`);
  }
}

/**
 * Complete the MFA verification step during login.
 */
export async function apiVerifyMfa(
  mfaToken: string,
  totpCode: string,
  config: ApiConfig = defaultConfig,
): Promise<{ token: string }> {
  const res = await apiPost(
    "/api/auth/mfa/verify",
    { mfa_token: mfaToken, totp_code: totpCode },
    undefined,
    config,
  );
  if (!res.ok) {
    const text = await res.text();
    throw new Error(`MFA verify failed (${res.status}): ${text}`);
  }
  return (await res.json()) as { token: string };
}

/**
 * Set MFA requirement settings for auth methods.
 */
export async function apiSetMfaRequired(
  settings: {
    mfa_required_password?: boolean;
    mfa_required_pki?: boolean;
    mfa_required_passkey?: boolean;
  },
  token: string,
  config: ApiConfig = defaultConfig,
): Promise<void> {
  await apiPut("/api/admin/settings", settings, token, config);
}
