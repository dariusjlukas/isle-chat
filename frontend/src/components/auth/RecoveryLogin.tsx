import { useState } from 'react';
import { Button, Card, CardBody, Input, Alert, Tabs, Tab } from '@heroui/react';
import { useChatStore } from '../../stores/chatStore';
import * as api from '../../services/api';
import logoLarge from '../../assets/isle-chat-logo-large.png';
import logoLargeDark from '../../assets/isle-chat-logo-large-dark.png';

interface Props {
  onSwitchToLogin: () => void;
}

export function RecoveryLogin({ onSwitchToLogin }: Props) {
  const setAuth = useChatStore((s) => s.setAuth);
  const [mode, setMode] = useState<string>('key');
  const [recoveryKey, setRecoveryKey] = useState('');
  const [recoveryToken, setRecoveryToken] = useState('');
  const [error, setError] = useState('');
  const [loading, setLoading] = useState(false);

  const handleKeySubmit = async (e: React.FormEvent) => {
    e.preventDefault();
    if (!recoveryKey.trim()) {
      setError('Please enter a recovery key');
      return;
    }

    setLoading(true);
    setError('');
    try {
      const result = await api.recoveryLogin(recoveryKey.trim());
      setAuth(result.user, result.token);
      if (result.must_setup_key) {
        alert(
          'You are logged in with a recovery key. Please add a new authentication method in Settings.',
        );
      }
    } catch (e) {
      setError(e instanceof Error ? e.message : 'Recovery failed');
    } finally {
      setLoading(false);
    }
  };

  const handleTokenSubmit = async (e: React.FormEvent) => {
    e.preventDefault();
    if (!recoveryToken.trim()) {
      setError('Please enter a recovery token');
      return;
    }

    setLoading(true);
    setError('');
    try {
      const result = await api.recoverAccount(recoveryToken.trim());
      setAuth(result.user, result.token);
      if (result.must_setup_key) {
        alert(
          'You are logged in with a recovery token. Please add a new authentication method in Settings.',
        );
      }
    } catch (e) {
      setError(e instanceof Error ? e.message : 'Recovery failed');
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
            Account Recovery
          </h1>
          <p className="text-default-500 mb-6">Regain access to your account</p>

          <Tabs
            selectedKey={mode}
            onSelectionChange={(key) => {
              setMode(key as string);
              setError('');
            }}
            className="mb-4"
            fullWidth
          >
            <Tab key="key" title="Recovery Key" />
            <Tab key="token" title="Recovery Token" />
          </Tabs>

          {error && (
            <Alert color="danger" variant="flat" className="mb-4">
              {error}
            </Alert>
          )}

          {mode === 'key' ? (
            <form onSubmit={handleKeySubmit} className="space-y-4">
              <Input
                label="Recovery Key"
                description="One of your 8 single-use recovery keys"
                variant="bordered"
                value={recoveryKey}
                onChange={(e) => setRecoveryKey(e.target.value)}
                placeholder="XXXX-XXXX-XXXX-XXXX-XXXX"
                classNames={{ input: 'font-mono' }}
              />
              <Button
                type="submit"
                color="primary"
                fullWidth
                isLoading={loading}
                size="lg"
              >
                {loading ? 'Recovering...' : 'Recover Account'}
              </Button>
            </form>
          ) : (
            <form onSubmit={handleTokenSubmit} className="space-y-4">
              <Input
                label="Recovery Token"
                description="Provided by your server admin"
                variant="bordered"
                value={recoveryToken}
                onChange={(e) => setRecoveryToken(e.target.value)}
                placeholder="Paste recovery token here"
                classNames={{ input: 'font-mono' }}
              />
              <Button
                type="submit"
                color="primary"
                fullWidth
                isLoading={loading}
                size="lg"
              >
                {loading ? 'Recovering...' : 'Recover Account'}
              </Button>
            </form>
          )}

          <Button
            variant="light"
            color="primary"
            fullWidth
            onPress={onSwitchToLogin}
            className="mt-4"
            size="sm"
          >
            Back to sign in
          </Button>
        </CardBody>
      </Card>
    </div>
  );
}
