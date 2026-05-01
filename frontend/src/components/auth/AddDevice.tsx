import { useState, useEffect } from 'react';
import { Button, Card, CardBody, Input, Alert, Divider } from '@heroui/react';
import { useChatStore } from '../../stores/chatStore';
import {
  browserSupportsWebAuthn,
  register as webauthnRegister,
} from '../../services/webauthn';
import * as pki from '../../services/pki';
import * as api from '../../services/api';
import type { PublicKeyCredentialCreationOptionsJSON } from '@simplewebauthn/browser';
import logoLight from '../../assets/enclavestation-light-mode-icon.png';
import logoDark from '../../assets/enclavestation-dark-mode-icon.png';

interface Props {
  initialToken?: string;
  onSwitchToLogin: () => void;
}

export function AddDevice({ initialToken, onSwitchToLogin }: Props) {
  const setAuth = useChatStore((s) => s.setAuth);
  const [token, setToken] = useState(initialToken || '');
  const [deviceName, setDeviceName] = useState('');
  const [pin, setPin] = useState('');
  const [confirmPin, setConfirmPin] = useState('');
  const [error, setError] = useState('');
  const [loading, setLoading] = useState('');
  const [authMethods, setAuthMethods] = useState<string[]>([]);
  const [serverName, setServerName] = useState('EnclaveStation');
  const [serverIconUrl, setServerIconUrl] = useState<string | null>(null);
  const [serverIconDarkUrl, setServerIconDarkUrl] = useState<string | null>(
    null,
  );
  const [configLoading, setConfigLoading] = useState(true);

  useEffect(() => {
    api.getPublicConfig().then((config) => {
      setAuthMethods(config.auth_methods);
      setServerName(config.server_name);
      if (config.server_icon_file_id) {
        setServerIconUrl(api.getAvatarUrl(config.server_icon_file_id));
      }
      if (config.server_icon_dark_file_id) {
        setServerIconDarkUrl(api.getAvatarUrl(config.server_icon_dark_file_id));
      }
      setConfigLoading(false);
    });
  }, []);

  const passkeysEnabled =
    authMethods.includes('passkey') && browserSupportsWebAuthn();
  const pkiEnabled = authMethods.includes('pki') && pki.isWebCryptoAvailable();

  const handlePasskeyLink = async () => {
    if (!token.trim()) {
      setError('Please enter a device token');
      return;
    }
    setError('');
    setLoading('passkey');
    try {
      const options = await api.addDevicePasskeyOptions(token.trim());
      const credential = await webauthnRegister(
        options as unknown as PublicKeyCredentialCreationOptionsJSON,
      );
      const result = await api.addDevicePasskeyVerify({
        id: credential.id,
        response: credential.response as unknown as Record<string, unknown>,
        device_name: deviceName || 'Passkey',
      });
      setAuth(result.user);
    } catch (e) {
      if (e instanceof Error && e.name === 'NotAllowedError') {
        setError('Passkey creation was cancelled');
      } else {
        setError(e instanceof Error ? e.message : 'Failed to link device');
      }
    } finally {
      setLoading('');
    }
  };

  const handlePkiLink = async () => {
    if (!token.trim()) {
      setError('Please enter a device token');
      return;
    }
    if (!pin) {
      setError('Please enter a PIN to protect your browser key');
      return;
    }
    if (pin !== confirmPin) {
      setError('PINs do not match');
      return;
    }
    setError('');
    setLoading('pki');
    try {
      const challengeResp = await api.addDevicePkiChallenge(token.trim());
      const publicKey = await pki.generateKeyPair(pin);
      const signature = await pki.signChallenge(challengeResp.challenge, pin);

      const result = await api.addDevicePki({
        device_token: token.trim(),
        public_key: publicKey,
        challenge: challengeResp.challenge,
        signature,
        device_name: deviceName || 'Browser Key',
      });
      setAuth(result.user);
    } catch (e) {
      setError(e instanceof Error ? e.message : 'Failed to link device');
    } finally {
      setLoading('');
    }
  };

  return (
    <div className='min-h-screen flex flex-col items-center justify-center bg-background'>
      <img
        src={serverIconUrl || logoLight}
        alt={serverName}
        className='w-24 h-24 mb-4 rounded-xl object-cover dark:hidden'
      />
      <img
        src={serverIconDarkUrl || logoDark}
        alt={serverName}
        className='w-24 h-24 mb-4 rounded-xl object-cover hidden dark:block'
      />
      <Card className='w-full max-w-md mx-4 sm:mx-auto shadow-2xl'>
        <CardBody className='p-5 sm:p-8'>
          <h1 className='text-3xl font-bold text-foreground mb-2'>
            Link Device
          </h1>
          <p className='text-default-500 mb-4'>
            Enter the device token from your primary device to link this browser
            to your account.
          </p>

          {error && (
            <Alert color='danger' variant='flat' className='mb-4'>
              {error}
            </Alert>
          )}

          <div className='space-y-3'>
            <Input
              label='Device Token'
              variant='bordered'
              value={token}
              onChange={(e) => setToken(e.target.value)}
              placeholder='Paste your device token here'
              size='sm'
            />
            <Input
              label='Device Name'
              variant='bordered'
              value={deviceName}
              onChange={(e) => setDeviceName(e.target.value)}
              placeholder='e.g. Work Laptop, Phone'
              size='sm'
            />

            {configLoading ? (
              <div className='text-center text-default-500 py-4'>
                Loading...
              </div>
            ) : (
              <>
                {passkeysEnabled && (
                  <Button
                    color='primary'
                    fullWidth
                    isLoading={loading === 'passkey'}
                    isDisabled={!!loading}
                    onPress={handlePasskeyLink}
                    size='lg'
                  >
                    {loading === 'passkey' ? 'Linking...' : 'Link with Passkey'}
                  </Button>
                )}

                {pkiEnabled && passkeysEnabled && (
                  <div className='relative my-2'>
                    <Divider />
                    <span className='absolute left-1/2 top-1/2 -translate-x-1/2 -translate-y-1/2 bg-content1 px-2 text-xs text-default-400'>
                      or
                    </span>
                  </div>
                )}

                {pkiEnabled && (
                  <>
                    <Input
                      label='Browser Key PIN'
                      type='password'
                      variant='bordered'
                      value={pin}
                      onChange={(e) => setPin(e.target.value)}
                      placeholder='Create a PIN for this device'
                      size='sm'
                    />
                    <Input
                      label='Confirm PIN'
                      type='password'
                      variant='bordered'
                      value={confirmPin}
                      onChange={(e) => setConfirmPin(e.target.value)}
                      placeholder='Confirm your PIN'
                      size='sm'
                    />
                    <Button
                      color='secondary'
                      fullWidth
                      isLoading={loading === 'pki'}
                      isDisabled={!!loading}
                      onPress={handlePkiLink}
                      size='lg'
                    >
                      {loading === 'pki'
                        ? 'Linking...'
                        : 'Link with Browser Key'}
                    </Button>
                  </>
                )}

                {!passkeysEnabled && !pkiEnabled && (
                  <Alert color='warning' variant='flat'>
                    No supported authentication methods are available for device
                    linking.
                  </Alert>
                )}
              </>
            )}
          </div>

          <Divider className='my-4' />

          <Button
            variant='light'
            color='primary'
            fullWidth
            onPress={onSwitchToLogin}
            size='sm'
          >
            Back to sign in
          </Button>
        </CardBody>
      </Card>
    </div>
  );
}
