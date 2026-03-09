import { useState } from 'react';
import { Button, Input, Textarea } from '@heroui/react';
import { useChatStore } from '../../stores/chatStore';
import * as api from '../../services/api';

export function ProfileSettings() {
  const user = useChatStore((s) => s.user);
  const updateUser = useChatStore((s) => s.updateUser);

  const [displayName, setDisplayName] = useState(user?.display_name || '');
  const [bio, setBio] = useState(user?.bio || '');
  const [status, setStatus] = useState(user?.status || '');
  const [saving, setSaving] = useState(false);
  const [saveMsg, setSaveMsg] = useState('');

  const handleSaveProfile = async (e: React.FormEvent) => {
    e.preventDefault();
    setSaving(true);
    setSaveMsg('');
    try {
      const updated = await api.updateProfile({
        display_name: displayName.trim(),
        bio: bio.trim(),
        status: status.trim(),
      });
      updateUser({
        display_name: updated.display_name,
        bio: updated.bio,
        status: updated.status,
      });
      setSaveMsg('Saved');
      setTimeout(() => setSaveMsg(''), 2000);
    } catch (e) {
      setSaveMsg(e instanceof Error ? e.message : 'Failed to save');
    } finally {
      setSaving(false);
    }
  };

  return (
    <form onSubmit={handleSaveProfile} className='space-y-3'>
      <Input
        label='Display Name'
        variant='bordered'
        value={displayName}
        onChange={(e) => setDisplayName(e.target.value)}
      />
      <Input
        label='Status'
        variant='bordered'
        value={status}
        onChange={(e) => setStatus(e.target.value)}
        maxLength={100}
        placeholder='What are you up to?'
      />
      <Textarea
        label='Bio'
        variant='bordered'
        value={bio}
        onChange={(e) => setBio(e.target.value)}
        minRows={3}
        placeholder='Tell us about yourself'
      />
      <div className='flex items-center gap-3'>
        <Button type='submit' color='primary' isLoading={saving} size='sm'>
          {saving ? 'Saving...' : 'Save'}
        </Button>
        {saveMsg && (
          <span
            className={`text-sm ${saveMsg === 'Saved' ? 'text-success' : 'text-danger'}`}
          >
            {saveMsg}
          </span>
        )}
      </div>
    </form>
  );
}
