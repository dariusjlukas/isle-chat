/**
 * Direct API helpers for E2E test setup.
 * Used to set up preconditions (create users, set settings) without going through the UI.
 */

const BACKEND_PORT = process.env.TEST_BACKEND_PORT ?? "9099";
const API_BASE = `http://localhost:${BACKEND_PORT}`;

const PG_USER = process.env.POSTGRES_USER ?? "chatapp_test";
const PG_DB = process.env.POSTGRES_DB ?? "chatapp_test";
const PG_CONTAINER =
  process.env.TEST_PG_CONTAINER ?? "chatapp-test-postgres";

import { execSync } from "child_process";

async function apiPost(
  path: string,
  body: unknown,
  token?: string,
): Promise<Response> {
  const headers: Record<string, string> = {
    "Content-Type": "application/json",
  };
  if (token) headers["Authorization"] = `Bearer ${token}`;
  return fetch(`${API_BASE}${path}`, {
    method: "POST",
    headers,
    body: JSON.stringify(body),
  });
}

async function apiPut(
  path: string,
  body: unknown,
  token?: string,
): Promise<Response> {
  const headers: Record<string, string> = {
    "Content-Type": "application/json",
  };
  if (token) headers["Authorization"] = `Bearer ${token}`;
  return fetch(`${API_BASE}${path}`, {
    method: "PUT",
    headers,
    body: JSON.stringify(body),
  });
}

async function apiGet(path: string, token?: string): Promise<Response> {
  const headers: Record<string, string> = {};
  if (token) headers["Authorization"] = `Bearer ${token}`;
  return fetch(`${API_BASE}${path}`, { headers });
}

/**
 * Register a user via the API using PKI auth.
 * This bypasses the UI for faster test setup.
 * We use the SubtleCrypto-compatible approach via Node's crypto module.
 */
export async function apiRegisterUser(
  username: string,
  displayName: string,
): Promise<{ token: string; userId: string; recoveryKeys: string[] }> {
  // Generate ECDSA P-256 key pair using Node crypto
  const { generateKeyPairSync, createSign } = await import("crypto");
  const { privateKey, publicKey } = generateKeyPairSync("ec", {
    namedCurve: "prime256v1",
  });

  // Export public key as SPKI DER, then base64url encode
  const spkiDer = publicKey.export({ type: "spki", format: "der" });
  const pubKeyB64url = spkiDer
    .toString("base64")
    .replace(/\+/g, "-")
    .replace(/\//g, "_")
    .replace(/=+$/, "");

  // Get a challenge
  const challengeRes = await apiPost("/api/auth/pki/challenge", {});
  const { challenge } = (await challengeRes.json()) as { challenge: string };

  // Sign the challenge (DER format, need to convert to raw r||s)
  const signer = createSign("SHA256");
  signer.update(challenge);
  const derSig = signer.sign(privateKey);

  // Parse DER signature to extract r and s
  // DER: 0x30 <len> 0x02 <rlen> <r> 0x02 <slen> <s>
  let offset = 2; // skip 0x30 <len>
  offset += 1; // skip 0x02
  const rLen = derSig[offset++];
  const rBytes = derSig.subarray(offset, offset + rLen);
  offset += rLen;
  offset += 1; // skip 0x02
  const sLen = derSig[offset++];
  const sBytes = derSig.subarray(offset, offset + sLen);

  // Pad/trim to 32 bytes each
  const r = Buffer.alloc(32);
  const s = Buffer.alloc(32);
  rBytes.subarray(rBytes.length > 32 ? rBytes.length - 32 : 0).copy(r, Math.max(0, 32 - rBytes.length));
  sBytes.subarray(sBytes.length > 32 ? sBytes.length - 32 : 0).copy(s, Math.max(0, 32 - sBytes.length));
  const rawSig = Buffer.concat([r, s]);
  const sigB64url = rawSig
    .toString("base64")
    .replace(/\+/g, "-")
    .replace(/\//g, "_")
    .replace(/=+$/, "");

  // Register
  const regRes = await apiPost("/api/auth/pki/register", {
    username,
    display_name: displayName,
    public_key: pubKeyB64url,
    challenge,
    signature: sigB64url,
  });
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
export function promoteToOwner(userId: string): void {
  const sql = `UPDATE users SET role = 'owner' WHERE id = '${userId}'`;
  execSync(
    `docker exec ${PG_CONTAINER} psql -U "${PG_USER}" -d "${PG_DB}" -c "${sql}"`,
    { stdio: "pipe" },
  );
}

/**
 * Set registration mode to "open" so new users can register freely.
 */
export async function setRegistrationOpen(token: string): Promise<void> {
  await apiPut("/api/admin/settings", { registration_mode: "open" }, token);
}

/**
 * Complete the server setup wizard so it doesn't appear on login.
 */
export async function completeSetup(token: string): Promise<void> {
  await apiPost(
    "/api/admin/setup",
    { registration_mode: "open" },
    token,
  );
}

/**
 * Change a user's server role via the admin API.
 */
export async function apiChangeUserRole(
  userId: string,
  role: string,
  token: string,
): Promise<Response> {
  return apiPut(`/api/admin/users/${userId}/role`, { role }, token);
}

/**
 * Get admin users list via the API.
 */
export async function apiGetAdminUsers(
  token: string,
): Promise<Array<{ id: string; username: string; role: string }>> {
  const res = await apiGet("/api/admin/users", token);
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
): Promise<{ id: string; name: string }> {
  const res = await apiPost(
    "/api/channels",
    { name, ...options },
    token,
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
): Promise<{ id: string; name: string }> {
  const res = await apiPost(
    `/api/spaces/${spaceId}/channels`,
    { name, ...options },
    token,
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
): Promise<{ id: string; name: string }> {
  const res = await apiPost(
    "/api/spaces",
    { name, ...options },
    token,
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
): Promise<{ id: string }> {
  const res = await apiPost(
    `/api/channels/${channelId}/messages`,
    { content },
    token,
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
): Promise<void> {
  const res = await apiPost(`/api/spaces/${spaceId}/join`, {}, token);
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
): Promise<void> {
  const res = await apiPost(`/api/channels/${channelId}/join`, {}, token);
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
): Promise<Array<{ id: string; name: string }>> {
  const res = await apiGet("/api/channels", token);
  return (await res.json()) as Array<{ id: string; name: string }>;
}
