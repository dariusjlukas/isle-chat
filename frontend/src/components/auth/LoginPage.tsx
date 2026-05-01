import { useState, useEffect } from 'react';
import { Button, Card, CardBody, Input, Alert, Divider } from '@heroui/react';
import QRCode from 'qrcode';
import { useChatStore } from '../../stores/chatStore';
import { browserSupportsWebAuthn, authenticate } from '../../services/webauthn';
import * as pki from '../../services/pki';
import * as api from '../../services/api';
import type { LoginResult } from '../../services/api';
import logoLight from '../../assets/enclavestation-light-mode-icon.png';
import logoDark from '../../assets/enclavestation-dark-mode-icon.png';

interface Props {
  onSwitchToRegister: () => void;
  onSwitchToRecovery: () => void;
  onSwitchToAddDevice: () => void;
}

export function LoginPage({
  onSwitchToRegister,
  onSwitchToRecovery,
  onSwitchToAddDevice,
}: Props) {
  const setAuth = useChatStore((s) => s.setAuth);
  const authError = useChatStore((s) => s.authError);
  const [error, setError] = useState(authError || '');
  const [loading, setLoading] = useState('');
  const [authMethods, setAuthMethods] = useState<string[]>([]);
  const [hasLocalKey, setHasLocalKey] = useState(false);
  const [loginUsername, setLoginUsername] = useState('');
  const [loginPassword, setLoginPassword] = useState('');
  const [serverName, setServerName] = useState('EnclaveStation');
  const [serverIconUrl, setServerIconUrl] = useState<string | null>(null);
  const [serverIconDarkUrl, setServerIconDarkUrl] = useState<string | null>(
    null,
  );
  const [configLoading, setConfigLoading] = useState(true);
  const [serverDown, setServerDown] = useState(false);

  // PKI PIN state
  const [showPkiPin, setShowPkiPin] = useState(false);
  const [pkiPin, setPkiPin] = useState('');

  // MFA state
  const [mfaToken, setMfaToken] = useState('');
  const [totpCode, setTotpCode] = useState('');
  const [mfaLoading, setMfaLoading] = useState(false);

  // Forced TOTP setup state
  const [setupToken, setSetupToken] = useState('');
  const [setupSecret, setSetupSecret] = useState('');
  const [setupQrDataUrl, setSetupQrDataUrl] = useState('');
  const [setupCode, setSetupCode] = useState('');
  const [setupLoading, setSetupLoading] = useState(false);
  const [setupVerifying, setSetupVerifying] = useState(false);

  useEffect(() => {
    let retryTimer: ReturnType<typeof setTimeout>;
    let isRetry = false;
    const loadConfig = () => {
      if (!isRetry) setConfigLoading(true);
      Promise.all([
        api.getPublicConfig().then((config) => {
          setAuthMethods(config.auth_methods);
          setServerName(config.server_name);
          if (config.server_icon_file_id) {
            setServerIconUrl(api.getAvatarUrl(config.server_icon_file_id));
          }
          if (config.server_icon_dark_file_id) {
            setServerIconDarkUrl(
              api.getAvatarUrl(config.server_icon_dark_file_id),
            );
          }
          setServerDown(false);
        }),
        pki.hasStoredKey().then(setHasLocalKey),
      ])
        .catch(() => {
          setServerDown(true);
          isRetry = true;
          retryTimer = setTimeout(loadConfig, 5000);
        })
        .finally(() => setConfigLoading(false));
    };
    loadConfig();
    return () => clearTimeout(retryTimer);
  }, []);

  const handleLoginResult = async (result: LoginResult) => {
    if ('mfa_required' in result && result.mfa_required) {
      setMfaToken(result.mfa_token);
      setError('');
      return;
    }
    if ('must_setup_totp' in result && result.must_setup_totp) {
      // Start TOTP setup flow
      setSetupToken(result.mfa_token);
      setSetupLoading(true);
      setError('');
      try {
        const setup = await api.mfaSetup(result.mfa_token);
        setSetupSecret(setup.secret);
        const dataUrl = await QRCode.toDataURL(setup.uri, {
          width: 200,
          margin: 2,
          color: { dark: '#000000', light: '#ffffff' },
        });
        setSetupQrDataUrl(dataUrl);
      } catch (e) {
        setError(e instanceof Error ? e.message : 'Failed to start TOTP setup');
        setSetupToken('');
      } finally {
        setSetupLoading(false);
      }
      return;
    }
    setAuth(result.user);
  };

  const handleSetupVerify = async (code: string) => {
    if (code.length !== 6) return;
    setSetupVerifying(true);
    setError('');
    try {
      const result = await api.mfaSetupVerify({
        mfa_token: setupToken,
        code,
      });
      setAuth(result.user);
    } catch (e) {
      setError(e instanceof Error ? e.message : 'Verification failed');
      setSetupCode('');
    } finally {
      setSetupVerifying(false);
    }
  };

  const handleMfaVerify = async (code: string) => {
    if (code.length !== 6) return;
    setMfaLoading(true);
    setError('');
    try {
      const result = await api.verifyMfa({
        mfa_token: mfaToken,
        totp_code: code,
      });
      setAuth(result.user);
    } catch (e) {
      setError(e instanceof Error ? e.message : 'Verification failed');
      setTotpCode('');
    } finally {
      setMfaLoading(false);
    }
  };

  const handlePasskeyLogin = async () => {
    setLoading('passkey');
    setError('');
    try {
      const options = await api.getLoginOptions();
      const credential = await authenticate(options);
      const result = await api.verifyLogin(credential);
      handleLoginResult(result);
    } catch (e) {
      if (e instanceof Error && e.name === 'NotAllowedError') {
        setError('Authentication was cancelled');
      } else {
        setError(e instanceof Error ? e.message : 'Login failed');
      }
    } finally {
      setLoading('');
    }
  };

  const handlePkiLoginStart = () => {
    setError('');
    setShowPkiPin(true);
    setPkiPin('');
  };

  const handlePkiLogin = async (e?: React.FormEvent) => {
    if (e) e.preventDefault();
    if (!pkiPin) {
      setError('Please enter your PIN');
      return;
    }
    setLoading('pki');
    setError('');
    try {
      const publicKey = await pki.getStoredPublicKey();
      if (!publicKey) {
        setError('No browser key found. You may need to use a recovery key.');
        return;
      }
      const { challenge } = await api.getPkiChallenge(publicKey);
      const signature = await pki.signChallenge(challenge, pkiPin);
      const result = await api.pkiLogin({
        public_key: publicKey,
        challenge,
        signature,
      });
      handleLoginResult(result);
    } catch (e) {
      if (e instanceof Error && e.message === 'Incorrect PIN') {
        setError('Incorrect PIN');
      } else {
        setError(e instanceof Error ? e.message : 'Login failed');
      }
    } finally {
      setLoading('');
    }
  };

  const handlePasswordLogin = async (e: React.FormEvent) => {
    e.preventDefault();
    if (!loginUsername.trim() || !loginPassword) {
      setError('Username and password are required');
      return;
    }
    setLoading('password');
    setError('');
    try {
      const result = await api.passwordLogin({
        username: loginUsername.trim(),
        password: loginPassword,
      });
      handleLoginResult(result);
    } catch (e) {
      setError(e instanceof Error ? e.message : 'Login failed');
    } finally {
      setLoading('');
    }
  };

  const passkeysEnabled = authMethods.includes('passkey');
  const pkiEnabled = authMethods.includes('pki') && pki.isWebCryptoAvailable();
  const passwordEnabled = authMethods.includes('password');
  const webauthnSupported = browserSupportsWebAuthn();

  const renderServerIcon = (size = 'w-48 h-48') => (
    <>
      <img
        src={serverIconUrl || logoLight}
        alt={serverName}
        className={`${size} mb-4 rounded-xl object-cover dark:hidden`}
      />
      <img
        src={serverIconDarkUrl || logoDark}
        alt={serverName}
        className={`${size} mb-4 rounded-xl object-cover hidden dark:block`}
      />
    </>
  );

  // Forced TOTP setup screen
  if (setupToken) {
    return (
      <div className='min-h-screen flex flex-col items-center justify-center bg-background'>
        {renderServerIcon()}
        <Card className='w-full max-w-md mx-4 sm:mx-auto shadow-2xl'>
          <CardBody className='p-5 sm:p-8'>
            <h1 className='text-3xl font-bold text-foreground mb-2'>
              Set Up Two-Factor Authentication
            </h1>
            <p className='text-default-500 mb-4'>
              This server requires two-factor authentication. Scan the QR code
              with your authenticator app to continue.
            </p>

            {error && (
              <Alert color='danger' variant='flat' className='mb-4'>
                {error}
              </Alert>
            )}

            {setupLoading ? (
              <div className='text-center text-default-500 py-8'>
                Preparing setup...
              </div>
            ) : (
              <>
                <div className='flex justify-center py-2'>
                  {setupQrDataUrl && (
                    <img
                      src={setupQrDataUrl}
                      alt='TOTP QR Code'
                      className='rounded-lg'
                      width={200}
                      height={200}
                    />
                  )}
                </div>

                <div className='mb-4'>
                  <p className='text-xs text-default-400 mb-1'>
                    Or enter this key manually:
                  </p>
                  <code className='text-xs bg-default-100 px-2 py-1 rounded font-mono select-all break-all'>
                    {setupSecret}
                  </code>
                </div>

                <Divider className='my-4' />

                <div className='space-y-4'>
                  <Input
                    label='Verification Code'
                    variant='bordered'
                    value={setupCode}
                    onChange={(e) => {
                      const code = e.target.value
                        .replace(/\D/g, '')
                        .slice(0, 6);
                      setSetupCode(code);
                      if (code.length === 6) handleSetupVerify(code);
                    }}
                    placeholder='000000'
                    maxLength={6}
                    inputMode='numeric'
                    autoComplete='one-time-code'
                    autoFocus
                    isDisabled={setupVerifying}
                    classNames={{
                      input: 'text-center font-mono text-lg tracking-widest',
                    }}
                  />
                  {setupVerifying && (
                    <p className='text-center text-default-500 text-sm'>
                      Verifying...
                    </p>
                  )}
                </div>
              </>
            )}

            <Button
              variant='light'
              color='default'
              fullWidth
              className='mt-4'
              size='sm'
              onPress={() => {
                setSetupToken('');
                setSetupSecret('');
                setSetupQrDataUrl('');
                setSetupCode('');
                setError('');
              }}
            >
              Back to sign in
            </Button>
          </CardBody>
        </Card>
      </div>
    );
  }

  // MFA verification screen
  if (mfaToken) {
    return (
      <div className='min-h-screen flex flex-col items-center justify-center bg-background'>
        {renderServerIcon()}
        <Card className='w-full max-w-md mx-4 sm:mx-auto shadow-2xl'>
          <CardBody className='p-5 sm:p-8'>
            <h1 className='text-3xl font-bold text-foreground mb-2'>
              Two-Factor Authentication
            </h1>
            <p className='text-default-500 mb-6'>
              Enter the 6-digit code from your authenticator app
            </p>

            {error && (
              <Alert color='danger' variant='flat' className='mb-4'>
                {error}
              </Alert>
            )}

            <div className='space-y-4'>
              <Input
                label='Verification Code'
                variant='bordered'
                value={totpCode}
                onChange={(e) => {
                  const code = e.target.value.replace(/\D/g, '').slice(0, 6);
                  setTotpCode(code);
                  if (code.length === 6) handleMfaVerify(code);
                }}
                placeholder='000000'
                maxLength={6}
                inputMode='numeric'
                autoComplete='one-time-code'
                autoFocus
                isDisabled={mfaLoading}
                classNames={{
                  input: 'text-center font-mono text-lg tracking-widest',
                }}
              />
              {mfaLoading && (
                <p className='text-center text-default-500 text-sm'>
                  Verifying...
                </p>
              )}
            </div>

            <Button
              variant='light'
              color='default'
              fullWidth
              className='mt-4'
              size='sm'
              onPress={() => {
                setMfaToken('');
                setTotpCode('');
                setError('');
              }}
            >
              Back to sign in
            </Button>
          </CardBody>
        </Card>
      </div>
    );
  }

  return (
    <div className='min-h-screen flex flex-col items-center justify-center bg-background'>
      {renderServerIcon()}
      <Card className='w-full max-w-md mx-4 sm:mx-auto shadow-2xl'>
        <CardBody className='p-5 sm:p-8'>
          <h1 className='text-3xl font-bold text-foreground mb-2'>
            {serverName}
          </h1>
          <p className='text-default-500 mb-6'>Sign in to continue</p>

          {error && (
            <Alert color='danger' variant='flat' className='mb-4'>
              {error}
            </Alert>
          )}

          {serverDown ? (
            <Alert color='danger' variant='flat' className='mb-4'>
              Unable to reach the server. Retrying...
            </Alert>
          ) : configLoading ? (
            <div className='text-center text-default-500 py-4'>Loading...</div>
          ) : (
            <div className='space-y-3'>
              {passkeysEnabled && (
                <>
                  {!webauthnSupported ? (
                    <Alert color='warning' variant='flat'>
                      Your browser does not support passkeys.
                    </Alert>
                  ) : (
                    <Button
                      color='primary'
                      fullWidth
                      isLoading={loading === 'passkey'}
                      isDisabled={!!loading}
                      onPress={handlePasskeyLogin}
                      size='lg'
                    >
                      {loading === 'passkey'
                        ? 'Authenticating...'
                        : 'Sign in with Passkey'}
                    </Button>
                  )}
                </>
              )}

              {pkiEnabled && hasLocalKey && !showPkiPin && (
                <Button
                  color='secondary'
                  fullWidth
                  isDisabled={!!loading}
                  onPress={handlePkiLoginStart}
                  size='lg'
                >
                  Sign in with Browser Key
                </Button>
              )}

              {pkiEnabled && hasLocalKey && showPkiPin && (
                <form onSubmit={handlePkiLogin} className='space-y-3'>
                  <Input
                    label='Browser Key PIN'
                    type='password'
                    variant='bordered'
                    value={pkiPin}
                    onChange={(e) => setPkiPin(e.target.value)}
                    autoFocus
                    size='sm'
                  />
                  <div className='flex gap-2'>
                    <Button
                      variant='bordered'
                      onPress={() => {
                        setShowPkiPin(false);
                        setPkiPin('');
                        setError('');
                      }}
                      size='lg'
                      className='min-w-0'
                    >
                      Back
                    </Button>
                    <Button
                      type='submit'
                      color='secondary'
                      fullWidth
                      isLoading={loading === 'pki'}
                      isDisabled={!!loading}
                      size='lg'
                    >
                      {loading === 'pki'
                        ? 'Authenticating...'
                        : 'Unlock & Sign in'}
                    </Button>
                  </div>
                </form>
              )}

              {pkiEnabled && !hasLocalKey && (
                <p className='text-sm text-default-400 text-center'>
                  No browser key on this device
                </p>
              )}

              {passwordEnabled && (passkeysEnabled || pkiEnabled) && (
                <div className='relative my-2'>
                  <Divider />
                  <span className='absolute left-1/2 top-1/2 -translate-x-1/2 -translate-y-1/2 bg-content1 px-2 text-xs text-default-400'>
                    or
                  </span>
                </div>
              )}

              {passwordEnabled && (
                <form onSubmit={handlePasswordLogin} className='space-y-3'>
                  <Input
                    label='Username'
                    variant='bordered'
                    value={loginUsername}
                    onChange={(e) => setLoginUsername(e.target.value)}
                    size='sm'
                  />
                  <Input
                    label='Password'
                    type='password'
                    variant='bordered'
                    value={loginPassword}
                    onChange={(e) => setLoginPassword(e.target.value)}
                    size='sm'
                  />
                  <Button
                    type='submit'
                    color='primary'
                    variant={
                      passkeysEnabled || pkiEnabled ? 'bordered' : 'solid'
                    }
                    fullWidth
                    isLoading={loading === 'password'}
                    isDisabled={!!loading}
                    size='lg'
                  >
                    {loading === 'password'
                      ? 'Signing in...'
                      : 'Sign in with Password'}
                  </Button>
                </form>
              )}
            </div>
          )}

          <Divider className='my-4' />

          <div className='flex flex-col gap-2 text-center'>
            <Button
              variant='light'
              color='primary'
              fullWidth
              onPress={onSwitchToRegister}
              isDisabled={serverDown || configLoading}
              size='sm'
            >
              Create an account
            </Button>
            <Button
              variant='light'
              color='default'
              fullWidth
              onPress={onSwitchToAddDevice}
              isDisabled={serverDown || configLoading}
              size='sm'
            >
              Link existing account to this device
            </Button>
            <Button
              variant='light'
              color='default'
              fullWidth
              onPress={onSwitchToRecovery}
              isDisabled={serverDown || configLoading}
              size='sm'
            >
              Use a recovery key
            </Button>
          </div>
        </CardBody>
      </Card>
    </div>
  );
}
