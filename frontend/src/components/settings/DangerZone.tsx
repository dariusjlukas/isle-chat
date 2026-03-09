import { useState } from 'react';
import { Button, Input, Alert } from '@heroui/react';
import { useChatStore } from '../../stores/chatStore';
import * as api from '../../services/api';

export function DangerZone() {
  const user = useChatStore((s) => s.user);
  const clearAuth = useChatStore((s) => s.clearAuth);

  const [deleteConfirm, setDeleteConfirm] = useState('');
  const [deleting, setDeleting] = useState(false);
  const [deleteError, setDeleteError] = useState('');

  const handleDeleteAccount = async () => {
    if (deleteConfirm !== user?.username) return;
    setDeleting(true);
    setDeleteError('');
    try {
      await api.deleteAccount();
      clearAuth();
    } catch (e) {
      setDeleteError(
        e instanceof Error ? e.message : 'Failed to delete account',
      );
      setDeleting(false);
    }
  };

  return (
    <div className='space-y-4'>
      <div>
        <p className='text-sm text-default-500 mb-2'>
          Permanently delete your account and all associated data. This cannot
          be undone.
        </p>
        {deleteError && (
          <Alert color='danger' variant='flat' className='mb-2'>
            {deleteError}
          </Alert>
        )}
        <div className='flex gap-2'>
          <Input
            size='sm'
            variant='bordered'
            color='danger'
            value={deleteConfirm}
            onChange={(e) => setDeleteConfirm(e.target.value)}
            placeholder={`Type "${user?.username}" to confirm`}
            className='flex-1'
          />
          <Button
            color='danger'
            size='sm'
            isDisabled={deleteConfirm !== user?.username}
            isLoading={deleting}
            onPress={handleDeleteAccount}
          >
            {deleting ? 'Deleting...' : 'Delete Account'}
          </Button>
        </div>
      </div>
    </div>
  );
}
