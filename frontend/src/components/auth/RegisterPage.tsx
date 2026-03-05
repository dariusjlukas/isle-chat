import { useState } from 'react';
import { Button, Card, CardBody, Input, Alert } from '@heroui/react';
import { useChatStore } from '../../stores/chatStore';
import { generateKeyPair } from '../../services/crypto';
import * as api from '../../services/api';
import logoLarge from '../../assets/isle-chat-logo-large.png';
import logoLargeDark from '../../assets/isle-chat-logo-large-dark.png';

interface Props {
  onSwitchToLogin: () => void;
}

export function RegisterPage({ onSwitchToLogin }: Props) {
  const setAuth = useChatStore((s) => s.setAuth);
  const [username, setUsername] = useState('');
  const [displayName, setDisplayName] = useState('');
  const [inviteToken, setInviteToken] = useState('');
  const [error, setError] = useState('');
  const [loading, setLoading] = useState(false);

  const handleRegister = async (e: React.FormEvent) => {
    e.preventDefault();
    if (!username.trim() || !displayName.trim()) {
      setError('Username and display name are required');
      return;
    }

    setLoading(true);
    setError('');
    try {
      const { publicKeyPem } = await generateKeyPair(username.trim());

      const result = await api.register({
        username: username.trim(),
        display_name: displayName.trim(),
        public_key: publicKeyPem,
        token: inviteToken.trim() || undefined,
      });

      setAuth(result.user, result.token);
    } catch (e) {
      setError(e instanceof Error ? e.message : 'Registration failed');
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
          <h1 className="text-3xl font-bold text-foreground mb-2">Register</h1>
          <p className="text-default-500 mb-6">
            Create your account with a cryptographic key
          </p>

          {error && (
            <Alert color="danger" variant="flat" className="mb-4">
              {error}
            </Alert>
          )}

          <form onSubmit={handleRegister} className="space-y-4">
            <Input
              label="Username"
              variant="bordered"
              value={username}
              onChange={(e) => setUsername(e.target.value)}
              placeholder="johndoe"
            />
            <Input
              label="Display Name"
              variant="bordered"
              value={displayName}
              onChange={(e) => setDisplayName(e.target.value)}
              placeholder="John Doe"
            />
            <Input
              label="Invite Token"
              description="Not needed for first user"
              variant="bordered"
              value={inviteToken}
              onChange={(e) => setInviteToken(e.target.value)}
              placeholder="Paste invite token here"
            />
            <Button
              type="submit"
              color="primary"
              fullWidth
              isLoading={loading}
              size="lg"
            >
              {loading
                ? 'Creating account...'
                : 'Create Account & Generate Keys'}
            </Button>
          </form>

          <p className="mt-4 text-center text-sm text-default-500">
            A cryptographic keypair will be generated and stored in your
            browser.
          </p>

          <Button
            variant="light"
            color="primary"
            fullWidth
            onPress={onSwitchToLogin}
            className="mt-4"
            size="sm"
          >
            Already registered? Sign in
          </Button>
        </CardBody>
      </Card>
    </div>
  );
}
