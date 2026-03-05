import { useState } from 'react';
import { Button, Card, CardBody, Input, Alert } from '@heroui/react';
import { generateKeyPair } from '../../services/crypto';
import * as api from '../../services/api';
import logoLarge from '../../assets/isle-chat-logo-large.png';
import logoLargeDark from '../../assets/isle-chat-logo-large-dark.png';

interface Props {
  onSwitchToLogin: () => void;
}

export function RequestAccess({ onSwitchToLogin }: Props) {
  const [username, setUsername] = useState('');
  const [displayName, setDisplayName] = useState('');
  const [error, setError] = useState('');
  const [success, setSuccess] = useState('');
  const [loading, setLoading] = useState(false);

  const handleRequest = async (e: React.FormEvent) => {
    e.preventDefault();
    if (!username.trim() || !displayName.trim()) {
      setError('Username and display name are required');
      return;
    }

    setLoading(true);
    setError('');
    try {
      const { publicKeyPem } = await generateKeyPair(username.trim());

      const result = await api.requestAccess({
        username: username.trim(),
        display_name: displayName.trim(),
        public_key: publicKeyPem,
      });

      setSuccess(result.message);
    } catch (e) {
      setError(e instanceof Error ? e.message : 'Request failed');
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
            Request Access
          </h1>
          <p className="text-default-500 mb-6">
            Submit a request to join this server
          </p>

          {error && (
            <Alert color="danger" variant="flat" className="mb-4">
              {error}
            </Alert>
          )}

          {success ? (
            <div className="space-y-4">
              <Alert color="success" variant="flat">
                {success}
              </Alert>
              <p className="text-default-500 text-sm">
                Your keys have been generated and stored. Once an admin approves
                your request, you can sign in using the login page.
              </p>
              <Button color="primary" fullWidth onPress={onSwitchToLogin}>
                Go to Login
              </Button>
            </div>
          ) : (
            <>
              <form onSubmit={handleRequest} className="space-y-4">
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
                <Button
                  type="submit"
                  color="primary"
                  fullWidth
                  isLoading={loading}
                  size="lg"
                >
                  {loading ? 'Submitting...' : 'Request Access'}
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
            </>
          )}
        </CardBody>
      </Card>
    </div>
  );
}
