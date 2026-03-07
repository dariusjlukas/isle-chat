import { useState, useEffect } from 'react';
import { Button, Card, CardBody, Alert, Spinner } from '@heroui/react';
import * as pki from '../../services/pki';
import * as api from '../../services/api';
import { RecoveryKeyDisplay } from '../auth/RecoveryKeyDisplay';

interface PkiKey {
  id: string;
  device_name: string;
  created_at: string;
}

export function PkiKeyManager() {
  const [keys, setKeys] = useState<PkiKey[]>([]);
  const [loading, setLoading] = useState(true);
  const [adding, setAdding] = useState(false);
  const [error, setError] = useState('');
  const [recoveryCount, setRecoveryCount] = useState(0);
  const [recoveryKeys, setRecoveryKeys] = useState<string[] | null>(null);
  const [regenerating, setRegenerating] = useState(false);

  useEffect(() => {
    loadData();
  }, []);

  const loadData = async () => {
    try {
      const [keysData, countData] = await Promise.all([
        api.listPkiKeys(),
        api.getRecoveryKeyCount(),
      ]);
      setKeys(keysData);
      setRecoveryCount(countData.remaining);
    } catch (e) {
      setError(e instanceof Error ? e.message : 'Failed to load keys');
    } finally {
      setLoading(false);
    }
  };

  const handleAddKey = async () => {
    setError('');
    setAdding(true);
    try {
      const { challenge } = await api.getPkiKeyChallenge();
      const publicKey = await pki.generateKeyPair();
      const signature = await pki.signChallenge(challenge);
      const result = await api.addPkiKey({
        public_key: publicKey,
        challenge,
        signature,
      });
      if (result.recovery_keys) {
        setRecoveryKeys(result.recovery_keys);
      }
      await loadData();
    } catch (e) {
      setError(e instanceof Error ? e.message : 'Failed to add key');
    } finally {
      setAdding(false);
    }
  };

  const handleRemoveKey = async (id: string) => {
    if (
      !confirm('Remove this browser key? It will no longer be able to sign in.')
    )
      return;
    setError('');
    try {
      await api.removePkiKey(id);
      setKeys(keys.filter((k) => k.id !== id));
    } catch (e) {
      setError(e instanceof Error ? e.message : 'Failed to remove key');
    }
  };

  const handleRegenerateRecovery = async () => {
    if (
      !confirm('This will invalidate all existing recovery keys. Are you sure?')
    )
      return;
    setRegenerating(true);
    setError('');
    try {
      const result = await api.regenerateRecoveryKeys();
      setRecoveryKeys(result.recovery_keys);
      setRecoveryCount(result.recovery_keys.length);
    } catch (e) {
      setError(e instanceof Error ? e.message : 'Failed to regenerate');
    } finally {
      setRegenerating(false);
    }
  };

  if (recoveryKeys) {
    return (
      <RecoveryKeyDisplay
        keys={recoveryKeys}
        onDone={() => {
          setRecoveryKeys(null);
          loadData();
        }}
      />
    );
  }

  if (loading)
    return (
      <div className='flex justify-center py-4'>
        <Spinner size='sm' />
      </div>
    );

  return (
    <div className='space-y-4'>
      <h3 className='text-lg font-semibold text-foreground'>Browser Keys</h3>

      {error && (
        <Alert color='danger' variant='flat'>
          {error}
        </Alert>
      )}

      <div className='space-y-2'>
        {keys.map((key) => (
          <Card key={key.id}>
            <CardBody className='flex-row items-center justify-between py-2'>
              <div>
                <div className='text-sm text-foreground'>{key.device_name}</div>
                <div className='text-xs text-default-500'>
                  Added {new Date(key.created_at).toLocaleDateString()}
                </div>
              </div>
              <Button
                color='danger'
                variant='light'
                size='sm'
                onPress={() => handleRemoveKey(key.id)}
              >
                Remove
              </Button>
            </CardBody>
          </Card>
        ))}
        {keys.length === 0 && (
          <p className='text-sm text-default-400'>No browser keys registered</p>
        )}
      </div>

      <Button
        color='primary'
        fullWidth
        onPress={handleAddKey}
        isLoading={adding}
      >
        {adding ? 'Generating key...' : 'Add Browser Key'}
      </Button>

      {recoveryCount > 0 && (
        <div className='pt-2 border-t border-divider'>
          <div className='flex items-center justify-between'>
            <span className='text-sm text-default-500'>
              {recoveryCount} recovery key{recoveryCount !== 1 ? 's' : ''}{' '}
              remaining
            </span>
            <Button
              variant='bordered'
              size='sm'
              onPress={handleRegenerateRecovery}
              isLoading={regenerating}
            >
              Regenerate
            </Button>
          </div>
        </div>
      )}
    </div>
  );
}
