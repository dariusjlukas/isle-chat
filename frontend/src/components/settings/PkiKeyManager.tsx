import { useState, useEffect } from 'react';
import { Button, Card, CardBody, Input, Alert, Spinner } from '@heroui/react';
import * as pki from '../../services/pki';
import * as api from '../../services/api';
import { useChatStore } from '../../stores/chatStore';
import { useSettingsNav } from '../common/settingsNavContext';
import { RecoveryKeyDisplay } from '../auth/RecoveryKeyDisplay';

interface PkiKey {
  id: string;
  device_name: string;
  created_at: string;
}

export function PkiKeyManager() {
  const user = useChatStore((s) => s.user);
  const navigateTo = useSettingsNav();
  const [keys, setKeys] = useState<PkiKey[]>([]);
  const [loading, setLoading] = useState(true);
  const [adding, setAdding] = useState(false);
  const [error, setError] = useState('');
  const [recoveryCount, setRecoveryCount] = useState(0);
  const [recoveryKeys, setRecoveryKeys] = useState<string[] | null>(null);
  const [regenerating, setRegenerating] = useState(false);
  const [showPinForm, setShowPinForm] = useState(false);
  const [newPin, setNewPin] = useState('');
  const [confirmPin, setConfirmPin] = useState('');
  const [showChangePinForm, setShowChangePinForm] = useState(false);
  const [currentPin, setCurrentPin] = useState('');
  const [changePinNew, setChangePinNew] = useState('');
  const [changePinConfirm, setChangePinConfirm] = useState('');
  const [changePinLoading, setChangePinLoading] = useState(false);
  const [mfaRequired, setMfaRequired] = useState(false);

  useEffect(() => {
    api.getPublicConfig().then((config) => {
      setMfaRequired(config.mfa_required_pki ?? false);
    });
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

  const handleAddKeyStart = () => {
    setShowPinForm(true);
    setNewPin('');
    setConfirmPin('');
    setError('');
  };

  const handleAddKey = async (e?: React.FormEvent) => {
    if (e) e.preventDefault();
    if (!newPin) {
      setError('Please set a PIN to protect your browser key');
      return;
    }
    if (newPin !== confirmPin) {
      setError('PINs do not match');
      return;
    }
    if (newPin.length < 4) {
      setError('PIN must be at least 4 characters');
      return;
    }
    setError('');
    setAdding(true);
    try {
      const { challenge } = await api.getPkiKeyChallenge();
      const publicKey = await pki.generateKeyPair(newPin);
      const signature = await pki.signChallenge(challenge, newPin);
      const result = await api.addPkiKey({
        public_key: publicKey,
        challenge,
        signature,
      });
      if (result.recovery_keys) {
        setRecoveryKeys(result.recovery_keys);
      }
      setShowPinForm(false);
      await loadData();
    } catch (e) {
      setError(e instanceof Error ? e.message : 'Failed to add key');
    } finally {
      setAdding(false);
    }
  };

  const handleChangePin = async (e?: React.FormEvent) => {
    if (e) e.preventDefault();
    if (!currentPin) {
      setError('Please enter your current PIN');
      return;
    }
    if (!changePinNew) {
      setError('Please enter a new PIN');
      return;
    }
    if (changePinNew !== changePinConfirm) {
      setError('New PINs do not match');
      return;
    }
    if (changePinNew.length < 4) {
      setError('PIN must be at least 4 characters');
      return;
    }
    setError('');
    setChangePinLoading(true);
    try {
      await pki.changePin(currentPin, changePinNew);
      setShowChangePinForm(false);
      setCurrentPin('');
      setChangePinNew('');
      setChangePinConfirm('');
    } catch (e) {
      setError(e instanceof Error ? e.message : 'Failed to change PIN');
    } finally {
      setChangePinLoading(false);
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

      {showPinForm ? (
        <form onSubmit={handleAddKey} className='space-y-3'>
          <Input
            label='New PIN'
            type='password'
            variant='bordered'
            value={newPin}
            onChange={(e) => setNewPin(e.target.value)}
            description='Min 4 characters. This PIN encrypts your private key.'
            autoFocus
            size='sm'
          />
          <Input
            label='Confirm PIN'
            type='password'
            variant='bordered'
            value={confirmPin}
            onChange={(e) => setConfirmPin(e.target.value)}
            size='sm'
          />
          <div className='flex gap-2'>
            <Button
              variant='bordered'
              onPress={() => {
                setShowPinForm(false);
                setError('');
              }}
            >
              Cancel
            </Button>
            <Button type='submit' color='primary' fullWidth isLoading={adding}>
              {adding ? 'Working...' : 'Add Browser Key'}
            </Button>
          </div>
        </form>
      ) : mfaRequired && !user?.has_totp && keys.length === 0 ? (
        <Alert color='warning' variant='flat'>
          <p className='text-sm'>
            This server requires two-factor authentication for browser key
            login. Please set up TOTP before adding a browser key.
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
          onPress={handleAddKeyStart}
          isLoading={adding}
        >
          {adding ? 'Generating key...' : 'Add Browser Key'}
        </Button>
      )}

      {!showPinForm && !showChangePinForm && keys.length > 0 && (
        <Button
          variant='light'
          color='default'
          fullWidth
          size='sm'
          onPress={() => {
            setShowChangePinForm(true);
            setCurrentPin('');
            setChangePinNew('');
            setChangePinConfirm('');
            setError('');
          }}
        >
          Change PIN
        </Button>
      )}

      {showChangePinForm && (
        <form onSubmit={handleChangePin} className='space-y-3'>
          <Input
            label='Current PIN'
            type='password'
            variant='bordered'
            value={currentPin}
            onChange={(e) => setCurrentPin(e.target.value)}
            autoFocus
            size='sm'
          />
          <Input
            label='New PIN'
            type='password'
            variant='bordered'
            value={changePinNew}
            onChange={(e) => setChangePinNew(e.target.value)}
            size='sm'
          />
          <Input
            label='Confirm New PIN'
            type='password'
            variant='bordered'
            value={changePinConfirm}
            onChange={(e) => setChangePinConfirm(e.target.value)}
            size='sm'
          />
          <div className='flex gap-2'>
            <Button
              variant='bordered'
              onPress={() => {
                setShowChangePinForm(false);
                setError('');
              }}
            >
              Cancel
            </Button>
            <Button
              type='submit'
              color='primary'
              fullWidth
              isLoading={changePinLoading}
            >
              {changePinLoading ? 'Changing...' : 'Change PIN'}
            </Button>
          </div>
        </form>
      )}

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
