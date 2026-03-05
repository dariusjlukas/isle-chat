import { useState, useEffect } from 'react';
import {
  Button,
  Card,
  CardBody,
  Select,
  SelectItem,
  Alert,
} from '@heroui/react';
import { useChatStore } from '../../stores/chatStore';
import {
  getStoredKeys,
  signChallenge,
  listStoredUsers,
} from '../../services/crypto';
import * as api from '../../services/api';
import logoLarge from '../../assets/isle-chat-logo-large.png';
import logoLargeDark from '../../assets/isle-chat-logo-large-dark.png';

interface Props {
  onSwitchToRegister: () => void;
  onSwitchToRequest: () => void;
  onSwitchToAddDevice: () => void;
}

export function LoginPage({
  onSwitchToRegister,
  onSwitchToRequest,
  onSwitchToAddDevice,
}: Props) {
  const setAuth = useChatStore((s) => s.setAuth);
  const [error, setError] = useState('');
  const [loading, setLoading] = useState(false);
  const [storedUsers, setStoredUsers] = useState<string[]>([]);
  const [selectedUser, setSelectedUser] = useState('');

  useEffect(() => {
    listStoredUsers().then((users) => {
      setStoredUsers(users);
      if (users.length === 1) setSelectedUser(users[0]);
    });
  }, []);

  const handleLogin = async () => {
    if (!selectedUser) {
      setError('Please select a user to sign in as');
      return;
    }
    setLoading(true);
    setError('');
    try {
      const keys = await getStoredKeys(selectedUser);
      if (!keys) {
        setError('Keys not found for this user.');
        setLoading(false);
        return;
      }

      const { challenge } = await api.requestChallenge(keys.publicKeyPem);
      const signature = await signChallenge(keys.privateKey, challenge);
      const result = await api.verifyChallenge(
        keys.publicKeyPem,
        challenge,
        signature,
      );

      setAuth(result.user, result.token);
    } catch (e) {
      setError(e instanceof Error ? e.message : 'Login failed');
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
          <h1 className="text-3xl font-bold text-foreground mb-2">Isle Chat</h1>
          <p className="text-default-500 mb-6">
            Sign in with your cryptographic key
          </p>

          {error && (
            <Alert color="danger" variant="flat" className="mb-4">
              {error}
            </Alert>
          )}

          {storedUsers.length === 0 ? (
            <div className="text-default-500 text-center py-4 mb-4">
              No keys found on this device. Register a new account or link an
              existing one.
            </div>
          ) : storedUsers.length === 1 ? (
            <Button
              color="primary"
              fullWidth
              isLoading={loading}
              onPress={handleLogin}
              size="lg"
            >
              {loading ? 'Authenticating...' : `Sign in as ${selectedUser}`}
            </Button>
          ) : (
            <div className="space-y-3">
              <Select
                label="Select account"
                variant="bordered"
                selectedKeys={selectedUser ? [selectedUser] : []}
                onChange={(e) => setSelectedUser(e.target.value)}
              >
                {storedUsers.map((u) => (
                  <SelectItem key={u}>{u}</SelectItem>
                ))}
              </Select>
              <Button
                color="primary"
                fullWidth
                isLoading={loading}
                isDisabled={!selectedUser}
                onPress={handleLogin}
                size="lg"
              >
                {loading ? 'Authenticating...' : 'Sign In'}
              </Button>
            </div>
          )}

          <div className="mt-6 flex flex-col gap-2 text-center">
            <Button
              variant="light"
              color="primary"
              fullWidth
              onPress={onSwitchToRegister}
              size="sm"
            >
              Have an invite? Register here
            </Button>
            <Button
              variant="light"
              color="primary"
              fullWidth
              onPress={onSwitchToAddDevice}
              size="sm"
            >
              Link existing account to this device
            </Button>
            <Button
              variant="light"
              color="default"
              fullWidth
              onPress={onSwitchToRequest}
              size="sm"
            >
              Request access
            </Button>
          </div>
        </CardBody>
      </Card>
    </div>
  );
}
