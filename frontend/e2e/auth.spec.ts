import { test, expect } from '@playwright/test';
import { mockUnauthenticatedAPI, PUBLIC_CONFIG } from './helpers';

test.describe('Login page', () => {
  test.beforeEach(async ({ page }) => {
    await mockUnauthenticatedAPI(page);
    await page.goto('/');
  });

  test('shows the server name and sign-in prompt', async ({ page }) => {
    await expect(page.getByText(PUBLIC_CONFIG.server_name)).toBeVisible();
    await expect(page.getByText('Sign in to continue')).toBeVisible();
  });

  test('shows passkey and browser-key login buttons', async ({ page }) => {
    await expect(
      page.getByRole('button', { name: 'Sign in with Passkey' }),
    ).toBeVisible();
    await expect(
      page.getByRole('button', { name: 'Sign in with Browser Key' }),
    ).toBeVisible();
  });

  test('shows create-account and recovery links', async ({ page }) => {
    await expect(
      page.getByRole('button', { name: 'Create an account' }),
    ).toBeVisible();
    await expect(
      page.getByRole('button', { name: 'Use a recovery key' }),
    ).toBeVisible();
  });

  test('navigates to the register page', async ({ page }) => {
    await page.getByRole('button', { name: 'Create an account' }).click();
    await expect(page.getByText('Register')).toBeVisible();
    await expect(page.getByText('Create your account')).toBeVisible();
  });

  test('navigates to the recovery page', async ({ page }) => {
    await page.getByRole('button', { name: 'Use a recovery key' }).click();
    await expect(page.getByText('Account Recovery')).toBeVisible();
    await expect(
      page.getByText('Regain access to your account'),
    ).toBeVisible();
  });

  test('shows server-down alert when API is unreachable', async ({ page }) => {
    // Override the config route to return an error
    await page.route('**/api/config/public', (route) =>
      route.fulfill({ status: 500, body: 'Internal Server Error' }),
    );
    await page.goto('/');
    await expect(
      page.getByText('Unable to reach the server'),
    ).toBeVisible();
  });

  test('respects custom server name from config', async ({ page }) => {
    await mockUnauthenticatedAPI(page, {
      config: { server_name: 'My Private Chat' },
    });
    await page.goto('/');
    await expect(page.getByText('My Private Chat')).toBeVisible();
  });
});

test.describe('Register page', () => {
  test.beforeEach(async ({ page }) => {
    await mockUnauthenticatedAPI(page);
    await page.goto('/');
    await page.getByRole('button', { name: 'Create an account' }).click();
  });

  test('shows username and display name inputs', async ({ page }) => {
    await expect(page.getByLabel('Username')).toBeVisible();
    await expect(page.getByLabel('Display Name')).toBeVisible();
  });

  test('shows invite token input in invite mode', async ({ page }) => {
    await expect(page.getByLabel('Invite Token')).toBeVisible();
  });

  test('hides invite token input in open mode', async ({ page }) => {
    await mockUnauthenticatedAPI(page, {
      config: { registration_mode: 'open' },
    });
    await page.goto('/');
    await page.getByRole('button', { name: 'Create an account' }).click();
    await expect(page.getByLabel('Invite Token')).not.toBeVisible();
  });

  test('shows auth method tabs when both are available', async ({ page }) => {
    await expect(page.getByRole('tab', { name: 'Passkey' })).toBeVisible();
    await expect(
      page.getByRole('tab', { name: 'Browser Key' }),
    ).toBeVisible();
  });

  test('has link back to login', async ({ page }) => {
    const backBtn = page.getByRole('button', {
      name: 'Already registered? Sign in',
    });
    await expect(backBtn).toBeVisible();
    await backBtn.click();
    await expect(page.getByText('Sign in to continue')).toBeVisible();
  });

  test('shows request access button in invite mode', async ({ page }) => {
    await expect(
      page.getByRole('button', { name: 'Request Access' }),
    ).toBeVisible();
  });
});

test.describe('Recovery page', () => {
  test.beforeEach(async ({ page }) => {
    await mockUnauthenticatedAPI(page);
    await page.goto('/');
    await page.getByRole('button', { name: 'Use a recovery key' }).click();
  });

  test('shows recovery key and recovery token tabs', async ({ page }) => {
    await expect(
      page.getByRole('tab', { name: 'Recovery Key' }),
    ).toBeVisible();
    await expect(
      page.getByRole('tab', { name: 'Recovery Token' }),
    ).toBeVisible();
  });

  test('shows recovery key input by default', async ({ page }) => {
    await expect(page.getByLabel('Recovery Key')).toBeVisible();
    await expect(
      page.getByText('One of your 8 single-use recovery keys'),
    ).toBeVisible();
  });

  test('switches to recovery token input', async ({ page }) => {
    await page.getByRole('tab', { name: 'Recovery Token' }).click();
    await expect(page.getByLabel('Recovery Token')).toBeVisible();
    await expect(
      page.getByText('Provided by your server admin'),
    ).toBeVisible();
  });

  test('has a recover button', async ({ page }) => {
    await expect(
      page.getByRole('button', { name: 'Recover Account' }),
    ).toBeVisible();
  });

  test('has link back to login', async ({ page }) => {
    const backBtn = page.getByRole('button', { name: 'Back to sign in' });
    await expect(backBtn).toBeVisible();
    await backBtn.click();
    await expect(page.getByText('Sign in to continue')).toBeVisible();
  });
});
