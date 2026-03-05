import { useState, useEffect, useRef, useCallback } from 'react';
import {
  Button,
  Card,
  CardBody,
  Input,
  Alert,
  Tabs,
  Tab,
  Spinner,
} from '@heroui/react';
import { useChatStore } from '../../stores/chatStore';
import { register as webauthnRegister } from '../../services/webauthn';
import type { User } from '../../types';
import * as pki from '../../services/pki';
import * as api from '../../services/api';
import { RecoveryKeyDisplay } from './RecoveryKeyDisplay';
import logoLarge from '../../assets/isle-chat-logo-large.png';
import logoLargeDark from '../../assets/isle-chat-logo-large-dark.png';

interface Props {
  onSwitchToLogin: () => void;
}

type Phase = 'form' | 'waiting' | 'recovery-keys';

export function RegisterPage({ onSwitchToLogin }: Props) {
  const setAuth = useChatStore((s) => s.setAuth);
  const [username, setUsername] = useState('');
  const [displayName, setDisplayName] = useState('');
  const [inviteToken, setInviteToken] = useState('');
  const [error, setError] = useState('');
  const [loading, setLoading] = useState(false);
  const [authMethods, setAuthMethods] = useState<string[]>([]);
  const [registrationMode, setRegistrationMode] = useState('invite');
  const [selectedMethod, setSelectedMethod] = useState<string>('passkey');
  const [configLoading, setConfigLoading] = useState(true);
  const [phase, setPhase] = useState<Phase>('form');

  const pollRef = useRef<ReturnType<typeof setInterval> | null>(null);

  // Recovery key display
  const [recoveryKeys, setRecoveryKeys] = useState<string[] | null>(null);
  const [pendingAuth, setPendingAuth] = useState<{
    user: User;
    token: string;
  } | null>(null);

  useEffect(() => {
    api
      .getPublicConfig()
      .then((config) => {
        setAuthMethods(config.auth_methods);
        setRegistrationMode(config.registration_mode);
        if (config.auth_methods.length === 1) {
          setSelectedMethod(config.auth_methods[0]);
        }
      })
      .finally(() => setConfigLoading(false));
  }, []);

  // Clean up polling on unmount
  useEffect(() => {
    return () => {
      if (pollRef.current) clearInterval(pollRef.current);
    };
  }, []);

  const startPolling = useCallback(
    (id: string) => {
      if (pollRef.current) clearInterval(pollRef.current);
      pollRef.current = setInterval(async () => {
        try {
          const result = await api.getRequestStatus(id);
          if (result.status === 'approved' && result.token && result.user) {
            if (pollRef.current) clearInterval(pollRef.current);
            setAuth(result.user, result.token);
          } else if (result.status === 'denied') {
            if (pollRef.current) clearInterval(pollRef.current);
            setPhase('form');
            setError('Your access request was denied.');
          }
        } catch {
          // polling error, keep trying
        }
      }, 3000);
    },
    [setAuth],
  );

  // --- Direct registration (with invite token or open mode) ---

  const handlePasskeyRegister = async () => {
    setLoading(true);
    setError('');
    try {
      const options = await api.getRegistrationOptions({
        username: username.trim(),
        display_name: displayName.trim(),
        token: inviteToken.trim() || undefined,
      });
      const credential = await webauthnRegister(options);
      const result = await api.verifyRegistration(credential);
      setAuth(result.user, result.token);
    } catch (e) {
      if (e instanceof Error && e.name === 'NotAllowedError') {
        setError('Passkey creation was cancelled');
      } else {
        setError(e instanceof Error ? e.message : 'Registration failed');
      }
    } finally {
      setLoading(false);
    }
  };

  const handlePkiRegister = async () => {
    setLoading(true);
    setError('');
    try {
      const { challenge } = await api.getPkiChallenge();
      const publicKey = await pki.generateKeyPair();
      const signature = await pki.signChallenge(challenge);
      const result = await api.pkiRegister({
        username: username.trim(),
        display_name: displayName.trim(),
        token: inviteToken.trim() || undefined,
        public_key: publicKey,
        challenge,
        signature,
      });
      setRecoveryKeys(result.recovery_keys);
      setPendingAuth({ user: result.user, token: result.token });
      setPhase('recovery-keys');
    } catch (e) {
      setError(e instanceof Error ? e.message : 'Registration failed');
    } finally {
      setLoading(false);
    }
  };

  const handleDirectRegister = (e: React.FormEvent) => {
    e.preventDefault();
    if (!username.trim() || !displayName.trim()) {
      setError('Username and display name are required');
      return;
    }
    if (selectedMethod === 'pki') {
      handlePkiRegister();
    } else {
      handlePasskeyRegister();
    }
  };

  // --- Request access flow (credentials generated before submission) ---

  const handleRequestAccess = async () => {
    if (!username.trim() || !displayName.trim()) {
      setError('Username and display name are required');
      return;
    }

    setLoading(true);
    setError('');
    try {
      if (selectedMethod === 'pki') {
        // PKI: generate key, get challenge, sign, submit
        const { challenge } = await api.getPkiChallenge();
        const publicKey = await pki.generateKeyPair();
        const signature = await pki.signChallenge(challenge);
        const result = await api.requestAccess({
          username: username.trim(),
          display_name: displayName.trim(),
          auth_method: 'pki',
          public_key: publicKey,
          challenge,
          signature,
        });

        setPhase('waiting');
        startPolling(result.request_id);
      } else {
        // Passkey: get WebAuthn options, create credential, submit
        const options = await api.requestAccessOptions({
          username: username.trim(),
          display_name: displayName.trim(),
        });
        const credential = await webauthnRegister(options);
        const result = await api.requestAccess({
          username: username.trim(),
          display_name: displayName.trim(),
          auth_method: 'passkey',
          credential,
        });

        setPhase('waiting');
        startPolling(result.request_id);
      }
    } catch (e) {
      if (e instanceof Error && e.name === 'NotAllowedError') {
        setError('Credential creation was cancelled');
      } else {
        setError(e instanceof Error ? e.message : 'Request failed');
      }
    } finally {
      setLoading(false);
    }
  };

  const handleRecoveryKeysDone = () => {
    if (pendingAuth) {
      setAuth(pendingAuth.user, pendingAuth.token);
    }
  };

  // --- Recovery keys display ---
  if (phase === 'recovery-keys' && recoveryKeys && pendingAuth) {
    return (
      <RecoveryKeyDisplay keys={recoveryKeys} onDone={handleRecoveryKeysDone} />
    );
  }

  const passkeysAvailable = authMethods.includes('passkey');
  const pkiAvailable = authMethods.includes('pki');
  const showTabs = passkeysAvailable && pkiAvailable;

  // Can user register directly? (with invite token in invite mode, or always in open mode)
  const canDirectRegister =
    registrationMode === 'open' || registrationMode === 'invite';

  // Can user request access? (invite or approval mode)
  const canRequestAccess =
    registrationMode === 'invite' || registrationMode === 'approval';

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
          <p className="text-default-500 mb-6">Create your account</p>

          {error && (
            <Alert color="danger" variant="flat" className="mb-4">
              {error}
            </Alert>
          )}

          {configLoading ? (
            <div className="flex justify-center py-4">
              <Spinner size="sm" />
            </div>
          ) : phase === 'waiting' ? (
            <div className="text-center space-y-4 py-4">
              <Spinner size="lg" />
              <p className="text-foreground font-medium">
                Waiting for admin approval...
              </p>
              <p className="text-sm text-default-500">
                Your request has been submitted. This page will automatically
                sign you in once an admin approves your request.
              </p>
              <Button
                variant="light"
                color="default"
                onPress={() => {
                  if (pollRef.current) clearInterval(pollRef.current);
                  setPhase('form');
                }}
                size="sm"
              >
                Cancel
              </Button>
            </div>
          ) : (
            <>
              {showTabs && (
                <Tabs
                  selectedKey={selectedMethod}
                  onSelectionChange={(key) => setSelectedMethod(key as string)}
                  className="mb-4"
                  color="primary"
                  variant="bordered"
                  fullWidth
                >
                  <Tab key="passkey" title="Passkey" />
                  <Tab key="pki" title="Browser Key" />
                </Tabs>
              )}

              <form onSubmit={handleDirectRegister} className="space-y-4">
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

                {canDirectRegister && registrationMode === 'invite' && (
                  <Input
                    label="Invite Token"
                    description="Not needed for first user"
                    variant="bordered"
                    value={inviteToken}
                    onChange={(e) => setInviteToken(e.target.value)}
                    placeholder="Paste invite token here"
                  />
                )}

                {canDirectRegister && (
                  <Button
                    type="submit"
                    color="primary"
                    fullWidth
                    isLoading={loading}
                    isDisabled={loading}
                    size="lg"
                  >
                    {loading
                      ? 'Creating account...'
                      : selectedMethod === 'pki'
                        ? 'Create Account with Browser Key'
                        : 'Create Account with Passkey'}
                  </Button>
                )}

                {canRequestAccess && (
                  <Button
                    type="button"
                    color={canDirectRegister ? 'default' : 'primary'}
                    variant={canDirectRegister ? 'bordered' : 'solid'}
                    fullWidth
                    isLoading={loading}
                    isDisabled={loading}
                    size={canDirectRegister ? 'md' : 'lg'}
                    onPress={handleRequestAccess}
                  >
                    {loading ? 'Setting up...' : 'Request Access'}
                  </Button>
                )}
              </form>

              <p className="mt-4 text-center text-sm text-default-500">
                {selectedMethod === 'pki'
                  ? 'A cryptographic key will be generated and stored securely in your browser.'
                  : 'A passkey will be created and stored securely by your device.'}
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
            </>
          )}
        </CardBody>
      </Card>
    </div>
  );
}
