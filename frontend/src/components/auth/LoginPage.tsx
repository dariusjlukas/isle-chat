import { useState, useEffect } from 'react';
import { Button, Card, CardBody, Alert, Divider } from '@heroui/react';
import { useChatStore } from '../../stores/chatStore';
import { browserSupportsWebAuthn, authenticate } from '../../services/webauthn';
import * as pki from '../../services/pki';
import * as api from '../../services/api';
import logoLarge from '../../assets/isle-chat-logo-large.png';
import logoLargeDark from '../../assets/isle-chat-logo-large-dark.png';

interface Props {
  onSwitchToRegister: () => void;
  onSwitchToRecovery: () => void;
}

export function LoginPage({ onSwitchToRegister, onSwitchToRecovery }: Props) {
  const setAuth = useChatStore((s) => s.setAuth);
  const [error, setError] = useState('');
  const [loading, setLoading] = useState('');
  const [authMethods, setAuthMethods] = useState<string[]>([]);
  const [hasLocalKey, setHasLocalKey] = useState(false);
  const [serverName, setServerName] = useState('Isle Chat');
  const [configLoading, setConfigLoading] = useState(true);
  const [serverDown, setServerDown] = useState(false);

  useEffect(() => {
    let retryTimer: ReturnType<typeof setTimeout>;
    let isRetry = false;
    const loadConfig = () => {
      if (!isRetry) setConfigLoading(true);
      Promise.all([
        api.getPublicConfig().then((config) => {
          setAuthMethods(config.auth_methods);
          setServerName(config.server_name);
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

  const handlePasskeyLogin = async () => {
    setLoading('passkey');
    setError('');
    try {
      const options = await api.getLoginOptions();
      const credential = await authenticate(options);
      const result = await api.verifyLogin(credential);
      setAuth(result.user, result.token);
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

  const handlePkiLogin = async () => {
    setLoading('pki');
    setError('');
    try {
      const publicKey = await pki.getStoredPublicKey();
      if (!publicKey) {
        setError('No browser key found. You may need to use a recovery key.');
        return;
      }
      const { challenge } = await api.getPkiChallenge(publicKey);
      const signature = await pki.signChallenge(challenge);
      const result = await api.pkiLogin({
        public_key: publicKey,
        challenge,
        signature,
      });
      setAuth(result.user, result.token);
    } catch (e) {
      setError(e instanceof Error ? e.message : 'Login failed');
    } finally {
      setLoading('');
    }
  };

  const passkeysEnabled = authMethods.includes('passkey');
  const pkiEnabled = authMethods.includes('pki') && pki.isWebCryptoAvailable();
  const webauthnSupported = browserSupportsWebAuthn();

  return (
    <div className='min-h-screen flex flex-col items-center justify-center bg-background'>
      <img
        src={logoLarge}
        alt={serverName}
        className='w-24 h-24 mb-4 dark:hidden'
      />
      <img
        src={logoLargeDark}
        alt={serverName}
        className='w-24 h-24 mb-4 hidden dark:block'
      />
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

              {pkiEnabled && hasLocalKey && (
                <Button
                  color='secondary'
                  fullWidth
                  isLoading={loading === 'pki'}
                  isDisabled={!!loading}
                  onPress={handlePkiLogin}
                  size='lg'
                >
                  {loading === 'pki'
                    ? 'Authenticating...'
                    : 'Sign in with Browser Key'}
                </Button>
              )}

              {pkiEnabled && !hasLocalKey && (
                <p className='text-sm text-default-400 text-center'>
                  No browser key on this device
                </p>
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
