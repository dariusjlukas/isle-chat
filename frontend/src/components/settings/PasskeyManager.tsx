import { useState, useEffect } from 'react';
import { Button, Card, CardBody, Alert, Spinner } from '@heroui/react';
import { register as webauthnRegister } from '../../services/webauthn';
import * as api from '../../services/api';

interface Passkey {
  id: string;
  device_name: string;
  created_at: string;
}

export function PasskeyManager() {
  const [passkeys, setPasskeys] = useState<Passkey[]>([]);
  const [loading, setLoading] = useState(true);
  const [adding, setAdding] = useState(false);
  const [error, setError] = useState('');

  useEffect(() => {
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

      <Button
        color='primary'
        fullWidth
        onPress={handleAddPasskey}
        isLoading={adding}
      >
        {adding ? 'Setting up passkey...' : 'Add Passkey'}
      </Button>
    </div>
  );
}
