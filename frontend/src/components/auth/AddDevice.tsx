import { useState } from 'react';
import { Button, Card, CardBody, Input, Alert } from '@heroui/react';
import { useChatStore } from '../../stores/chatStore';
import { generateKeyPair, storeKeysAs, clearKeys } from '../../services/crypto';
import * as api from '../../services/api';
import logoLarge from '../../assets/isle-chat-logo-large.png';
import logoLargeDark from '../../assets/isle-chat-logo-large-dark.png';

interface Props {
  onSwitchToLogin: () => void;
  initialToken?: string;
}

export function AddDevice({ onSwitchToLogin, initialToken = '' }: Props) {
  const setAuth = useChatStore((s) => s.setAuth);
  const [deviceToken, setDeviceToken] = useState(initialToken);
  const [deviceName, setDeviceName] = useState('');
  const [error, setError] = useState('');
  const [loading, setLoading] = useState(false);

  const handleAddDevice = async (e: React.FormEvent) => {
    e.preventDefault();
    if (!deviceToken.trim()) {
      setError('Device token is required');
      return;
    }
    setLoading(true);
    setError('');
    try {
      const { publicKeyPem } = await generateKeyPair('__pending_device__');

      const result = await api.addDevice({
        device_token: deviceToken.trim(),
        public_key: publicKeyPem,
        device_name: deviceName.trim() || 'New Device',
      });

      const { getStoredKeys } = await import('../../services/crypto');
      const keys = await getStoredKeys('__pending_device__');
      if (keys) {
        await storeKeysAs(
          result.user.username,
          keys.privateKey,
          keys.publicKeyPem,
        );
        await clearKeys('__pending_device__');
      }

      setAuth(result.user, result.token);
    } catch (e) {
      await clearKeys('__pending_device__');
      setError(e instanceof Error ? e.message : 'Failed to link device');
    } finally {
      setLoading(false);
    }
  };

  return (
    <div className="min-h-screen flex flex-col items-center justify-center bg-background">
      <img
        src={logoLarge}
        alt="Isle Chat"
        className="w-24 h-24 mb-4 dark:hidden"
      />
      <img
        src={logoLargeDark}
        alt="Isle Chat"
        className="w-24 h-24 mb-4 hidden dark:block"
      />
      <Card className="w-full max-w-md mx-4 sm:mx-auto shadow-2xl">
        <CardBody className="p-5 sm:p-8">
          <h1 className="text-3xl font-bold text-foreground mb-2">
            Link Device
          </h1>
          <p className="text-default-500 mb-6">
            Enter the device token from your existing device to link this
            browser to your account
          </p>

          {error && (
            <Alert color="danger" variant="flat" className="mb-4">
              {error}
            </Alert>
          )}

          <form onSubmit={handleAddDevice} className="space-y-4">
            <Input
              label="Device Token"
              variant="bordered"
              value={deviceToken}
              onChange={(e) => setDeviceToken(e.target.value)}
              placeholder="Paste token here"
              classNames={{ input: 'font-mono' }}
            />
            <Input
              label="Device Name"
              description="Optional"
              variant="bordered"
              value={deviceName}
              onChange={(e) => setDeviceName(e.target.value)}
              placeholder="e.g. Phone, Laptop, Work PC"
            />
            <Button
              type="submit"
              color="primary"
              fullWidth
              isLoading={loading}
              size="lg"
            >
              {loading ? 'Linking...' : 'Link Device'}
            </Button>
          </form>

          <Button
            variant="light"
            color="primary"
            fullWidth
            onPress={onSwitchToLogin}
            className="mt-4"
            size="sm"
          >
            Back to login
          </Button>
        </CardBody>
      </Card>
    </div>
  );
}
