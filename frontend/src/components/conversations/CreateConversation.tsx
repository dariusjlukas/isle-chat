import { useState } from 'react';
import {
  Modal,
  ModalContent,
  ModalHeader,
  ModalBody,
  ModalFooter,
  Button,
  Input,
  Alert,
} from '@heroui/react';
import * as api from '../../services/api';
import { useChatStore } from '../../stores/chatStore';
import { UserPicker } from '../common/UserPicker';

interface Props {
  onClose: () => void;
}

export function CreateConversation({ onClose }: Props) {
  const [name, setName] = useState('');
  const [selectedUsers, setSelectedUsers] = useState<string[]>([]);
  const [error, setError] = useState('');
  const [loading, setLoading] = useState(false);
  const currentUser = useChatStore((s) => s.user);
  const setActiveChannel = useChatStore((s) => s.setActiveChannel);
  const setActiveView = useChatStore((s) => s.setActiveView);

  const handleSubmit = async (e: React.FormEvent) => {
    e.preventDefault();
    if (selectedUsers.length === 0) {
      setError('Select at least one user');
      return;
    }
    setLoading(true);
    setError('');
    try {
      const ch = await api.createConversation(
        selectedUsers,
        selectedUsers.length > 1 ? name.trim() || undefined : undefined,
      );
      const channels = await api.listChannels();
      useChatStore.getState().setChannels(channels);
      setActiveView({ type: 'messages' });
      setActiveChannel(ch.id);
      onClose();
    } catch (e) {
      setError(e instanceof Error ? e.message : 'Failed');
    } finally {
      setLoading(false);
    }
  };

  return (
    <Modal
      isOpen
      onOpenChange={(open) => {
        if (!open) onClose();
      }}
      size='md'
      scrollBehavior='inside'
      backdrop='opaque'
    >
      <ModalContent>
        <form onSubmit={handleSubmit}>
          <ModalHeader>New Conversation</ModalHeader>
          <ModalBody>
            {error && (
              <Alert color='danger' variant='flat'>
                {error}
              </Alert>
            )}

            {selectedUsers.length > 1 && (
              <Input
                label='Group Name (optional)'
                variant='bordered'
                value={name}
                onChange={(e) => setName(e.target.value)}
                placeholder='e.g. Weekend Plans'
              />
            )}

            <UserPicker
              mode='multi'
              selected={selectedUsers}
              onChange={setSelectedUsers}
              excludeIds={currentUser ? [currentUser.id] : []}
              label='Select users to chat with:'
            />
          </ModalBody>
          <ModalFooter>
            <Button variant='light' color='default' onPress={onClose}>
              Cancel
            </Button>
            <Button
              type='submit'
              color='primary'
              isLoading={loading}
              isDisabled={selectedUsers.length === 0}
            >
              {selectedUsers.length > 1 ? 'Create Group Chat' : 'Open Chat'}
            </Button>
          </ModalFooter>
        </form>
      </ModalContent>
    </Modal>
  );
}
