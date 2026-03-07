import { useState, useEffect } from 'react';
import { Button, Card, CardBody } from '@heroui/react';
import * as api from '../../services/api';

export function InviteManager() {
  const [invites, setInvites] = useState<
    Array<{
      id: string;
      token: string;
      created_by: string;
      used: boolean;
      expires_at: string;
    }>
  >([]);
  const [loading, setLoading] = useState(false);

  const loadInvites = async () => {
    try {
      const data = await api.listInvites();
      setInvites(data);
    } catch (e) {
      console.error('Invite operation failed:', e);
    }
  };

  useEffect(() => {
    api
      .listInvites()
      .then(setInvites)
      .catch(() => {});
  }, []);

  const handleCreate = async () => {
    setLoading(true);
    try {
      await api.createInvite(24);
      await loadInvites();
    } catch (e) {
      console.error('Invite operation failed:', e);
    }
    setLoading(false);
  };

  const copyToken = (token: string) => {
    navigator.clipboard.writeText(token);
  };

  return (
    <div>
      <div className='flex justify-end mb-4'>
        <Button
          color='primary'
          size='sm'
          isLoading={loading}
          onPress={handleCreate}
        >
          Generate Invite
        </Button>
      </div>

      <div className='space-y-2'>
        {invites.map((inv) => (
          <Card key={inv.id} className={inv.used ? 'opacity-50' : ''}>
            <CardBody className='flex-row items-center justify-between py-3'>
              <div>
                <code className='text-sm text-success font-mono'>
                  {inv.token.substring(0, 16)}...
                </code>
                <p className='text-xs text-default-500 mt-1'>
                  {inv.used
                    ? 'Used'
                    : `Expires: ${new Date(inv.expires_at).toLocaleString()}`}
                </p>
              </div>
              {!inv.used && (
                <Button
                  variant='light'
                  color='primary'
                  size='sm'
                  onPress={() => copyToken(inv.token)}
                >
                  Copy
                </Button>
              )}
            </CardBody>
          </Card>
        ))}
        {invites.length === 0 && (
          <p className='text-default-500 text-sm'>No invites generated yet.</p>
        )}
      </div>
    </div>
  );
}
