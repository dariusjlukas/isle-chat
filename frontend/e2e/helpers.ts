import { type Page } from '@playwright/test';

/** Mock user returned by /api/users/me and stored in auth state */
export const TEST_USER = {
  id: 'user-1',
  username: 'testuser',
  display_name: 'Test User',
  role: 'admin' as const,
  is_online: true,
  bio: '',
  status: '',
};

/** A second user for conversation lists */
export const OTHER_USER = {
  id: 'user-2',
  username: 'otheruser',
  display_name: 'Other User',
  role: 'user' as const,
  is_online: false,
  bio: '',
  status: '',
};

/** Default public config returned by the server */
export const PUBLIC_CONFIG = {
  auth_methods: ['passkey', 'pki'],
  registration_mode: 'invite',
  server_name: 'Test Server',
  setup_completed: true,
};

/** A sample space */
export const TEST_SPACE = {
  id: 'space-1',
  name: 'General Space',
  description: 'The default space',
  icon: '',
  is_public: true,
  default_role: 'write' as const,
  my_role: 'admin' as const,
  created_at: '2025-01-01T00:00:00Z',
  members: [],
};

/** A sample channel */
export const TEST_CHANNEL = {
  id: 'chan-1',
  name: 'general',
  description: 'General discussion',
  is_direct: false,
  is_public: true,
  default_role: 'write' as const,
  my_role: 'write' as const,
  created_at: '2025-01-01T00:00:00Z',
  members: [],
  space_id: 'space-1',
};

/** A direct-message channel */
export const DM_CHANNEL = {
  id: 'chan-dm-1',
  name: 'dm',
  description: '',
  is_direct: true,
  is_public: false,
  default_role: 'write' as const,
  my_role: 'write' as const,
  created_at: '2025-01-01T00:00:00Z',
  members: [
    {
      id: TEST_USER.id,
      username: TEST_USER.username,
      display_name: TEST_USER.display_name,
      is_online: true,
      role: 'write' as const,
    },
    {
      id: OTHER_USER.id,
      username: OTHER_USER.username,
      display_name: OTHER_USER.display_name,
      is_online: false,
      role: 'write' as const,
    },
  ],
};

/**
 * Set up API route mocks for an unauthenticated session.
 * The login page needs /api/config/public to render.
 */
export async function mockUnauthenticatedAPI(
  page: Page,
  overrides: { config?: Partial<typeof PUBLIC_CONFIG> } = {},
) {
  const config = { ...PUBLIC_CONFIG, ...overrides.config };

  await page.route('**/api/config/public', (route) =>
    route.fulfill({ json: config }),
  );

  // Block WebSocket connections so tests don't hang
  await page.route('**/ws', (route) => route.abort());
}

/**
 * Set up API route mocks and localStorage for an authenticated session.
 * This lets tests skip the login flow and go straight to the main app.
 */
export async function mockAuthenticatedAPI(
  page: Page,
  overrides: {
    user?: Partial<typeof TEST_USER>;
    channels?: typeof TEST_CHANNEL[];
    spaces?: typeof TEST_SPACE[];
    config?: Partial<typeof PUBLIC_CONFIG>;
  } = {},
) {
  const user = { ...TEST_USER, ...overrides.user };
  const channels = overrides.channels ?? [TEST_CHANNEL];
  const spaces = overrides.spaces ?? [TEST_SPACE];
  const config = { ...PUBLIC_CONFIG, ...overrides.config };

  // Inject session token before navigation so useAuth picks it up
  await page.addInitScript(() => {
    localStorage.setItem('session_token', 'fake-session-token');
  });

  await page.route('**/api/users/me', (route) =>
    route.fulfill({ json: user }),
  );

  await page.route('**/api/config/public', (route) =>
    route.fulfill({ json: config }),
  );

  await page.route('**/api/channels', (route) => {
    if (route.request().method() === 'GET') {
      return route.fulfill({ json: channels });
    }
    return route.continue();
  });

  await page.route('**/api/users', (route) =>
    route.fulfill({ json: [user, OTHER_USER] }),
  );

  await page.route('**/api/spaces', (route) => {
    if (route.request().method() === 'GET') {
      return route.fulfill({ json: spaces });
    }
    return route.continue();
  });

  await page.route('**/api/spaces/invites', (route) =>
    route.fulfill({ json: [] }),
  );

  await page.route('**/api/admin/join-requests', (route) =>
    route.fulfill({ json: [] }),
  );

  // Block WebSocket connections
  await page.route('**/ws', (route) => route.abort());
}
