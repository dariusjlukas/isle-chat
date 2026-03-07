import { useState, useEffect } from 'react';
import { Button, Card, CardBody } from '@heroui/react';
import * as api from '../../services/api';
import { UserPicker } from '../common/UserPicker';

export function RecoveryTokenManager() {
  const [tokens, setTokens] = useState<
    Array<{
      id: string;
      token: string;
      created_by: string;
      for_user: string;
      for_user_id: string;
      used: boolean;
      expires_at: string;
      created_at: string;
    }>
  >([]);
  const [selectedUserId, setSelectedUserId] = useState<string[]>([]);
  const [loading, setLoading] = useState(false);

  const loadTokens = async () => {
    try {
      const data = await api.listRecoveryTokens();
      setTokens(data);
    } catch (e) {
      console.error('Recovery token operation failed:', e);
    }
  };

  useEffect(() => {
    api
      .listRecoveryTokens()
      .then(setTokens)
      .catch(() => {});
  }, []);

  const handleCreate = async () => {
    if (selectedUserId.length === 0) return;
    setLoading(true);
    try {
      await api.createRecoveryToken(selectedUserId[0]);
      await loadTokens();
      setSelectedUserId([]);
    } catch (e) {
      console.error('Recovery token operation failed:', e);
    }
    setLoading(false);
  };

  const copyToken = (token: string) => {
    navigator.clipboard.writeText(token);
  };

  return (
    <div>
      <div className='mb-4'>
        <UserPicker
          mode='single'
          selected={selectedUserId}
          onChange={setSelectedUserId}
          label='Select user'
          placeholder='Search users...'
        />
        <Button
          color='primary'
          size='sm'
          isLoading={loading}
          isDisabled={selectedUserId.length === 0}
          onPress={handleCreate}
          className='mt-2'
        >
          Generate
        </Button>
      </div>

      <div className='space-y-2'>
        {tokens.map((t) => (
          <Card key={t.id} className={t.used ? 'opacity-50' : ''}>
            <CardBody className='flex-row items-center justify-between py-3'>
              <div>
                <p className='text-sm text-default-700'>
                  For <span className='font-medium'>{t.for_user}</span>
                </p>
                <code className='text-xs text-success font-mono'>
                  {t.token.substring(0, 16)}...
                </code>
                <p className='text-xs text-default-500 mt-1'>
                  {t.used
                    ? 'Used'
                    : `Expires: ${new Date(t.expires_at).toLocaleString()}`}
                </p>
              </div>
              {!t.used && (
                <Button
                  variant='light'
                  color='primary'
                  size='sm'
                  onPress={() => copyToken(t.token)}
                >
                  Copy
                </Button>
              )}
            </CardBody>
          </Card>
        ))}
        {tokens.length === 0 && (
          <p className='text-default-500 text-sm'>
            No recovery tokens generated yet.
          </p>
        )}
      </div>
    </div>
  );
}
