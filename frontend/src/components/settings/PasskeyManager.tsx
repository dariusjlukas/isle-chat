import { useState, useEffect } from 'react';
import { Button, Card, CardBody, Alert, Spinner } from '@heroui/react';
import { register as webauthnRegister } from '../../services/webauthn';
import { useChatStore } from '../../stores/chatStore';
import { useSettingsNav } from '../common/settingsNavContext';
import * as api from '../../services/api';

interface Passkey {
  id: string;
  device_name: string;
  created_at: string;
}

export function PasskeyManager() {
  const user = useChatStore((s) => s.user);
  const navigateTo = useSettingsNav();
  const [passkeys, setPasskeys] = useState<Passkey[]>([]);
  const [loading, setLoading] = useState(true);
  const [adding, setAdding] = useState(false);
  const [error, setError] = useState('');
  const [mfaRequired, setMfaRequired] = useState(false);

  useEffect(() => {
    api.getPublicConfig().then((config) => {
      setMfaRequired(config.mfa_required_passkey ?? false);
    });
    loadPasskeys();
  }, []);

  const loadPasskeys = async () => {
    try {
      const data = await api.listPasskeys();
      setPasskeys(data);
    } catch (e) {
      setError(e instanceof Error ? e.message : 'Failed to load passkeys');
    } finally {
      setLoading(false);
    }
  };

  const handleAddPasskey = async () => {
    setError('');
    setAdding(true);
    try {
      const options = await api.getPasskeyRegistrationOptions();
      const credential = await webauthnRegister(options);
      await api.verifyPasskeyRegistration(credential);
      await loadPasskeys();
    } catch (e) {
      if (e instanceof Error && e.name === 'NotAllowedError') {
        setError('Passkey creation was cancelled');
      } else {
        setError(e instanceof Error ? e.message : 'Failed to add passkey');
      }
    } finally {
      setAdding(false);
    }
  };

  const handleRemovePasskey = async (id: string) => {
    if (!confirm('Remove this passkey? It will no longer be able to sign in.'))
      return;
    setError('');
    try {
      await api.removePasskey(id);
      setPasskeys(passkeys.filter((p) => p.id !== id));
    } catch (e) {
      setError(e instanceof Error ? e.message : 'Failed to remove passkey');
    }
  };

  if (loading)
    return (
      <div className='flex justify-center py-4'>
        <Spinner size='sm' />
      </div>
    );

  const needsMfaSetup = mfaRequired && !user?.has_totp && passkeys.length === 0;

  return (
    <div className='space-y-4'>
      <h3 className='text-lg font-semibold text-foreground'>Passkeys</h3>

      {error && (
        <Alert color='danger' variant='flat'>
          {error}
        </Alert>
      )}

      <div className='space-y-2'>
        {passkeys.map((passkey) => (
          <Card key={passkey.id}>
            <CardBody className='flex-row items-center justify-between py-2'>
              <div>
                <div className='text-sm text-foreground'>
                  {passkey.device_name}
                </div>
                <div className='text-xs text-default-500'>
                  Added {new Date(passkey.created_at).toLocaleDateString()}
                </div>
              </div>
              {passkeys.length > 1 && (
                <Button
                  color='danger'
                  variant='light'
                  size='sm'
                  onPress={() => handleRemovePasskey(passkey.id)}
                >
                  Remove
                </Button>
              )}
            </CardBody>
          </Card>
        ))}
      </div>

      {needsMfaSetup ? (
        <Alert color='warning' variant='flat'>
          <p className='text-sm'>
            This server requires two-factor authentication for passkey login.
            Please set up TOTP before adding a passkey.
          </p>
          <Button
            size='sm'
            color='warning'
            variant='solid'
            className='mt-2'
            onPress={() => navigateTo('two-factor')}
          >
            Set Up Two-Factor Auth
          </Button>
        </Alert>
      ) : (
        <Button
          color='primary'
          fullWidth
          onPress={handleAddPasskey}
          isLoading={adding}
        >
          {adding ? 'Setting up passkey...' : 'Add Passkey'}
        </Button>
      )}
    </div>
  );
}
