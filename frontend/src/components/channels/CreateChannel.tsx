import { useState } from 'react';
import {
  Modal,
  ModalContent,
  ModalHeader,
  ModalBody,
  ModalFooter,
  Button,
  Input,
  Select,
  SelectItem,
  Alert,
  Switch,
} from '@heroui/react';
import * as api from '../../services/api';
import { useChatStore } from '../../stores/chatStore';

interface Props {
  onClose: () => void;
  spaceId: string;
}

export function CreateChannel({ onClose, spaceId }: Props) {
  const [name, setName] = useState('');
  const [description, setDescription] = useState('');
  const [isPublic, setIsPublic] = useState(true);
  const [defaultRole, setDefaultRole] = useState('write');
  const [error, setError] = useState('');
  const [loading, setLoading] = useState(false);
  const setActiveChannel = useChatStore((s) => s.setActiveChannel);

  const handleSubmit = async (e: React.FormEvent) => {
    e.preventDefault();
    if (!name.trim()) {
      setError('Channel name is required');
      setLoading(false);
      return;
    }
    setLoading(true);
    setError('');
    try {
      const ch = await api.createSpaceChannel(
        spaceId,
        name.trim(),
        description.trim(),
        undefined,
        isPublic,
        defaultRole,
      );
      const channels = await api.listChannels();
      useChatStore.getState().setChannels(channels);
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
      backdrop='opaque'
    >
      <ModalContent>
        <form onSubmit={handleSubmit}>
          <ModalHeader>Create Channel</ModalHeader>
          <ModalBody>
            {error && (
              <Alert color='danger' variant='flat'>
                {error}
              </Alert>
            )}
            <Input
              label='Channel Name'
              variant='bordered'
              value={name}
              onChange={(e) => setName(e.target.value)}
              placeholder='e.g. engineering'
            />
            <Input
              label='Description'
              description='Optional'
              variant='bordered'
              value={description}
              onChange={(e) => setDescription(e.target.value)}
              placeholder="What's this channel about?"
            />
            <div className='flex items-center justify-between'>
              <div>
                <p className='text-sm font-medium text-foreground'>
                  Public Channel
                </p>
                <p className='text-xs text-default-400'>
                  {isPublic ? 'Anyone can find and join' : 'Invite only'}
                </p>
              </div>
              <Switch
                isSelected={isPublic}
                onValueChange={setIsPublic}
                size='sm'
              />
            </div>
            <Select
              label='Default Role for New Members'
              variant='bordered'
              selectedKeys={[defaultRole]}
              onChange={(e) => setDefaultRole(e.target.value)}
            >
              <SelectItem key='write'>Write (can send messages)</SelectItem>
              <SelectItem key='read'>Read Only (can view only)</SelectItem>
            </Select>
          </ModalBody>
          <ModalFooter>
            <Button variant='light' color='default' onPress={onClose}>
              Cancel
            </Button>
            <Button type='submit' color='primary' isLoading={loading}>
              Create
            </Button>
          </ModalFooter>
        </form>
      </ModalContent>
    </Modal>
  );
}
