import { test, expect } from '@playwright/test';
import {
  mockAuthenticatedAPI,
  TEST_USER,
  TEST_CHANNEL,
  TEST_SPACE,
  DM_CHANNEL,
} from './helpers';

test.describe('Authenticated app shell', () => {
  test.beforeEach(async ({ page }) => {
    await mockAuthenticatedAPI(page);
    await page.goto('/');
  });

  test('shows the header with Isle Chat branding', async ({ page }) => {
    await expect(page.getByAltText('Isle Chat').first()).toBeVisible();
  });

  test('shows the welcome message when no channel is selected', async ({
    page,
  }) => {
    await expect(page.getByText('Welcome to Isle Chat')).toBeVisible();
    await expect(
      page.getByText('Select a channel or start a conversation'),
    ).toBeVisible();
  });

  test('shows the admin button for admin users', async ({ page }) => {
    // TEST_USER has role "admin" — admin button (shield icon) should be visible
    await expect(page.getByRole('button', { name: 'Admin Panel' })).toBeVisible();
  });

  test('hides the admin button for regular users', async ({ page }) => {
    await mockAuthenticatedAPI(page, { user: { role: 'user' } });
    await page.goto('/');
    await expect(
      page.getByRole('button', { name: 'Admin Panel' }),
    ).not.toBeVisible();
  });

  test('shows the settings button', async ({ page }) => {
    await expect(
      page.getByRole('button', { name: 'User Settings' }),
    ).toBeVisible();
  });

  test('shows the logout button', async ({ page }) => {
    await expect(
      page.getByRole('button', { name: 'Logout' }),
    ).toBeVisible();
  });

  test('shows the footer bar with build info and links', async ({ page }) => {
    await expect(page.getByText('Isle Chat').last()).toBeVisible();
    await expect(page.getByRole('link', { name: 'GitHub' })).toBeVisible();
    await expect(page.getByRole('link', { name: 'License' })).toBeVisible();
  });
});

test.describe('Sidebar', () => {
  test('shows channels in the sidebar on desktop', async ({ page }) => {
    await mockAuthenticatedAPI(page, {
      channels: [TEST_CHANNEL],
      spaces: [TEST_SPACE],
    });
    await page.goto('/');

    // The sidebar should list the channel name
    await expect(page.getByText('general')).toBeVisible();
  });

  test('shows spaces in the sidebar', async ({ page }) => {
    await mockAuthenticatedAPI(page, {
      spaces: [
        TEST_SPACE,
        {
          ...TEST_SPACE,
          id: 'space-2',
          name: 'Engineering',
          icon: '',
        },
      ],
    });
    await page.goto('/');

    // Space names should appear somewhere in the sidebar/icon rail
    await expect(page.getByText('General Space')).toBeVisible();
  });
});

test.describe('Admin panel', () => {
  test('opens admin modal on button click', async ({ page }) => {
    await mockAuthenticatedAPI(page);
    await page.goto('/');

    await page.getByRole('button', { name: 'Admin Panel' }).click();
    await expect(page.getByText('Admin Panel')).toBeVisible();
    await expect(page.getByText('Server Settings')).toBeVisible();
    await expect(page.getByText('Invite Tokens')).toBeVisible();
    await expect(page.getByText('Account Recovery')).toBeVisible();
    await expect(page.getByText('Join Requests')).toBeVisible();
  });
});

test.describe('Logout', () => {
  test('logs out and returns to login page', async ({ page }) => {
    await mockAuthenticatedAPI(page);
    // Mock logout endpoint
    await page.route('**/api/auth/logout', (route) =>
      route.fulfill({ json: {} }),
    );
    await page.goto('/');

    await page.getByRole('button', { name: 'Logout' }).click();
    // Should be back on login page
    await expect(page.getByText('Sign in to continue')).toBeVisible();
  });
});

test.describe('Chat area', () => {
  test('shows channel header when a channel is active', async ({ page }) => {
    // Mock messages endpoint for the channel
    await mockAuthenticatedAPI(page, {
      channels: [TEST_CHANNEL],
      spaces: [TEST_SPACE],
    });
    await page.route('**/api/channels/chan-1/messages*', (route) =>
      route.fulfill({ json: [] }),
    );
    await page.route('**/api/channels/chan-1/read-receipts', (route) =>
      route.fulfill({ json: [] }),
    );
    await page.goto('/');

    // Click on the channel in the sidebar
    await page.getByText('general').click();

    // The header should now show the channel name
    await expect(
      page.locator('header').getByText('general'),
    ).toBeVisible();
  });
});

test.describe('Responsive', () => {
  test('mobile sidebar toggle works', async ({ page }) => {
    // Use a mobile viewport
    await page.setViewportSize({ width: 375, height: 667 });
    await mockAuthenticatedAPI(page, {
      channels: [TEST_CHANNEL],
      spaces: [TEST_SPACE],
    });
    await page.goto('/');

    // Sidebar should be hidden initially on mobile (translated off-screen)
    const sidebar = page.locator('aside');
    await expect(sidebar).toHaveCSS('transform', /translateX/);

    // Click the hamburger menu button to open the sidebar
    await page.locator('header button').first().click();

    // Sidebar should now be visible (translated to 0)
    await expect(sidebar).toBeVisible();
  });
});
