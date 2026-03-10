import { useState, useEffect } from 'react';
import { Button, Input, Alert, Divider } from '@heroui/react';
import QRCode from 'qrcode';
import { useChatStore } from '../../stores/chatStore';
import * as api from '../../services/api';

type Phase = 'loading' | 'disabled' | 'setup' | 'enabled';

export function TotpSettings() {
  const updateUser = useChatStore((s) => s.updateUser);
  const [phase, setPhase] = useState<Phase>('loading');
  const [error, setError] = useState('');
  const [success, setSuccess] = useState('');

  // Setup state
  const [secret, setSecret] = useState('');
  const [qrDataUrl, setQrDataUrl] = useState('');
  const [setupCode, setSetupCode] = useState('');
  const [verifying, setVerifying] = useState(false);

  // Disable state
  const [disableCode, setDisableCode] = useState('');
  const [disabling, setDisabling] = useState(false);

  useEffect(() => {
    api.getMe().then((me) => {
      setPhase(me.has_totp ? 'enabled' : 'disabled');
    });
  }, []);

  const handleStartSetup = async () => {
    setError('');
    setSuccess('');
    try {
      const result = await api.setupTotp();
      setSecret(result.secret);
      const dataUrl = await QRCode.toDataURL(result.uri, {
        width: 200,
        margin: 2,
        color: { dark: '#000000', light: '#ffffff' },
      });
      setQrDataUrl(dataUrl);
      setPhase('setup');
    } catch (e) {
      setError(e instanceof Error ? e.message : 'Failed to start setup');
    }
  };

  const handleVerifySetup = async (e: React.FormEvent) => {
    e.preventDefault();
    if (!setupCode.trim()) {
      setError('Please enter the verification code');
      return;
    }
    setVerifying(true);
    setError('');
    try {
      await api.verifyTotpSetup(setupCode.trim());
      setPhase('enabled');
      updateUser({ has_totp: true });
      setSuccess('Two-factor authentication enabled successfully');
      setSetupCode('');
      setSecret('');
      setQrDataUrl('');
    } catch (e) {
      setError(e instanceof Error ? e.message : 'Verification failed');
    } finally {
      setVerifying(false);
    }
  };

  const handleDisable = async (e: React.FormEvent) => {
    e.preventDefault();
    if (!disableCode.trim()) {
      setError('Please enter your verification code to disable');
      return;
    }
    setDisabling(true);
    setError('');
    try {
      await api.removeTotp(disableCode.trim());
      setPhase('disabled');
      updateUser({ has_totp: false });
      setSuccess('Two-factor authentication disabled');
      setDisableCode('');
    } catch (e) {
      setError(e instanceof Error ? e.message : 'Failed to disable');
    } finally {
      setDisabling(false);
    }
  };

  if (phase === 'loading') {
    return <div className='text-default-500 text-sm'>Loading...</div>;
  }

  return (
    <div className='space-y-4'>
      {error && (
        <Alert color='danger' variant='flat'>
          {error}
        </Alert>
      )}
      {success && (
        <Alert color='success' variant='flat'>
          {success}
        </Alert>
      )}

      {phase === 'disabled' && (
        <>
          <p className='text-sm text-default-500'>
            Add an extra layer of security by requiring a verification code from
            an authenticator app when you sign in.
          </p>
          <Button color='primary' size='sm' onPress={handleStartSetup}>
            Set Up Two-Factor Authentication
          </Button>
        </>
      )}

      {phase === 'setup' && (
        <>
          <p className='text-sm text-default-500'>
            Scan the QR code below with your authenticator app (e.g. Google
            Authenticator, Authy, 1Password), then enter the 6-digit code to
            verify.
          </p>

          <div className='flex justify-center py-2'>
            {qrDataUrl && (
              <img
                src={qrDataUrl}
                alt='TOTP QR Code'
                className='rounded-lg'
                width={200}
                height={200}
              />
            )}
          </div>

          <div>
            <p className='text-xs text-default-400 mb-1'>
              Or enter this key manually:
            </p>
            <code className='text-xs bg-default-100 px-2 py-1 rounded font-mono select-all break-all'>
              {secret}
            </code>
          </div>

          <Divider />

          <form onSubmit={handleVerifySetup} className='space-y-3'>
            <Input
              label='Verification Code'
              variant='bordered'
              value={setupCode}
              onChange={(e) =>
                setSetupCode(e.target.value.replace(/\D/g, '').slice(0, 6))
              }
              placeholder='000000'
              maxLength={6}
              inputMode='numeric'
              autoComplete='one-time-code'
              classNames={{
                input: 'text-center font-mono text-lg tracking-widest',
              }}
            />
            <div className='flex gap-2'>
              <Button
                type='submit'
                color='primary'
                size='sm'
                isLoading={verifying}
              >
                Verify and Enable
              </Button>
              <Button
                type='button'
                variant='flat'
                size='sm'
                onPress={() => {
                  setPhase('disabled');
                  setSetupCode('');
                  setSecret('');
                  setQrDataUrl('');
                  setError('');
                }}
              >
                Cancel
              </Button>
            </div>
          </form>
        </>
      )}

      {phase === 'enabled' && (
        <>
          <Alert color='success' variant='flat'>
            Two-factor authentication is enabled.
          </Alert>

          <Divider />

          <div>
            <p className='text-sm font-medium text-foreground mb-1'>
              Disable Two-Factor Authentication
            </p>
            <p className='text-xs text-default-400 mb-3'>
              Enter a verification code from your authenticator app to disable
              two-factor authentication.
            </p>
            <form onSubmit={handleDisable} className='space-y-3'>
              <Input
                label='Verification Code'
                variant='bordered'
                value={disableCode}
                onChange={(e) =>
                  setDisableCode(e.target.value.replace(/\D/g, '').slice(0, 6))
                }
                placeholder='000000'
                maxLength={6}
                inputMode='numeric'
                autoComplete='one-time-code'
                classNames={{
                  input: 'text-center font-mono text-lg tracking-widest',
                }}
              />
              <Button
                type='submit'
                color='danger'
                variant='flat'
                size='sm'
                isLoading={disabling}
              >
                Disable Two-Factor Auth
              </Button>
            </form>
          </div>
        </>
      )}
    </div>
  );
}
