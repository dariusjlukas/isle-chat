import { useState } from 'react';
import { Button, Card, CardBody } from '@heroui/react';
import { useChatStore } from '../../stores/chatStore';
import * as api from '../../services/api';

export function SpaceInviteNotification() {
  const spaceInvites = useChatStore((s) => s.spaceInvites);
  const removeSpaceInvite = useChatStore((s) => s.removeSpaceInvite);
  const [loading, setLoading] = useState<Record<string, boolean>>({});

  if (spaceInvites.length === 0) return null;

  const handleAccept = async (inviteId: string) => {
    setLoading((prev) => ({ ...prev, [inviteId]: true }));
    try {
      await api.acceptSpaceInvite(inviteId);
      removeSpaceInvite(inviteId);
    } catch (e) {
      console.error('Space invite operation failed:', e);
    }
    setLoading((prev) => ({ ...prev, [inviteId]: false }));
  };

  const handleDecline = async (inviteId: string) => {
    setLoading((prev) => ({ ...prev, [inviteId]: true }));
    try {
      await api.declineSpaceInvite(inviteId);
      removeSpaceInvite(inviteId);
    } catch (e) {
      console.error('Space invite operation failed:', e);
    }
    setLoading((prev) => ({ ...prev, [inviteId]: false }));
  };

  return (
    <div className='fixed bottom-4 right-4 z-50 flex flex-col gap-2 max-w-sm'>
      {spaceInvites.map((invite) => (
        <Card key={invite.id} shadow='lg' className='border border-primary/30'>
          <CardBody className='p-4'>
            <p className='text-sm font-semibold text-foreground'>
              {invite.space_icon && (
                <span className='mr-1'>{invite.space_icon}</span>
              )}
              Space Invite
            </p>
            <p className='text-sm text-default-600 mt-1'>
              <span className='font-medium'>@{invite.invited_by_username}</span>{' '}
              invited you to join{' '}
              <span className='font-medium'>{invite.space_name}</span> as{' '}
              <span className='text-default-400'>{invite.role}</span>
            </p>
            <div className='flex gap-2 mt-3'>
              <Button
                size='sm'
                color='primary'
                onPress={() => handleAccept(invite.id)}
                isLoading={loading[invite.id]}
              >
                Accept
              </Button>
              <Button
                size='sm'
                variant='flat'
                color='danger'
                onPress={() => handleDecline(invite.id)}
                isLoading={loading[invite.id]}
              >
                Decline
              </Button>
            </div>
          </CardBody>
        </Card>
      ))}
    </div>
  );
}
