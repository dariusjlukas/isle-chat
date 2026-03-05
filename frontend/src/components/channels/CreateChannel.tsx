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
  mode: 'channel' | 'dm';
}

export function CreateChannel({ onClose, mode }: Props) {
  const [name, setName] = useState('');
  const [description, setDescription] = useState('');
  const [selectedUser, setSelectedUser] = useState('');
  const [isPublic, setIsPublic] = useState(true);
  const [defaultRole, setDefaultRole] = useState('write');
  const [error, setError] = useState('');
  const [loading, setLoading] = useState(false);
  const users = useChatStore((s) => s.users);
  const currentUser = useChatStore((s) => s.user);
  const setActiveChannel = useChatStore((s) => s.setActiveChannel);

  const handleSubmit = async (e: React.FormEvent) => {
    e.preventDefault();
    setLoading(true);
    setError('');
    try {
      if (mode === 'dm') {
        if (!selectedUser) {
          setError('Select a user');
          setLoading(false);
          return;
        }
        const ch = await api.createDM(selectedUser);
        const channels = await api.listChannels();
        useChatStore.getState().setChannels(channels);
        setActiveChannel(ch.id);
      } else {
        if (!name.trim()) {
          setError('Channel name is required');
          setLoading(false);
          return;
        }
        const ch = await api.createChannel(
          name.trim(),
          description.trim(),
          undefined,
          isPublic,
          defaultRole,
        );
        const channels = await api.listChannels();
        useChatStore.getState().setChannels(channels);
        setActiveChannel(ch.id);
      }
      onClose();
    } catch (e) {
      setError(e instanceof Error ? e.message : 'Failed');
    } finally {
      setLoading(false);
    }
  };

  const allUsers = [
    ...(currentUser
      ? [
          {
            id: currentUser.id,
            label: `${currentUser.display_name} (@${currentUser.username}) (you)`,
          },
        ]
      : []),
    ...users
      .filter((u) => u.id !== currentUser?.id)
      .map((u) => ({ id: u.id, label: `${u.display_name} (@${u.username})` })),
  ];

  return (
    <Modal
      isOpen
      onOpenChange={(open) => {
        if (!open) onClose();
      }}
      size="md"
      backdrop="opaque"
    >
      <ModalContent>
        <form onSubmit={handleSubmit}>
          <ModalHeader>
            {mode === 'dm' ? 'New Direct Message' : 'Create Channel'}
          </ModalHeader>
          <ModalBody>
            {error && (
              <Alert color="danger" variant="flat">
                {error}
              </Alert>
            )}

            {mode === 'dm' ? (
              <Select
                label="Select User"
                variant="bordered"
                selectedKeys={selectedUser ? [selectedUser] : []}
                onChange={(e) => setSelectedUser(e.target.value)}
              >
                {allUsers.map((u) => (
                  <SelectItem key={u.id}>{u.label}</SelectItem>
                ))}
              </Select>
            ) : (
              <>
                <Input
                  label="Channel Name"
                  variant="bordered"
                  value={name}
                  onChange={(e) => setName(e.target.value)}
                  placeholder="e.g. engineering"
                />
                <Input
                  label="Description"
                  description="Optional"
                  variant="bordered"
                  value={description}
                  onChange={(e) => setDescription(e.target.value)}
                  placeholder="What's this channel about?"
                />
                <div className="flex items-center justify-between">
                  <div>
                    <p className="text-sm font-medium text-foreground">
                      Public Channel
                    </p>
                    <p className="text-xs text-default-400">
                      {isPublic ? 'Anyone can find and join' : 'Invite only'}
                    </p>
                  </div>
                  <Switch
                    isSelected={isPublic}
                    onValueChange={setIsPublic}
                    size="sm"
                  />
                </div>
                <Select
                  label="Default Role for New Members"
                  variant="bordered"
                  selectedKeys={[defaultRole]}
                  onChange={(e) => setDefaultRole(e.target.value)}
                >
                  <SelectItem key="write">Write (can send messages)</SelectItem>
                  <SelectItem key="read">Read Only (can view only)</SelectItem>
                </Select>
              </>
            )}
          </ModalBody>
          <ModalFooter>
            <Button variant="light" color="default" onPress={onClose}>
              Cancel
            </Button>
            <Button type="submit" color="primary" isLoading={loading}>
              {loading ? 'Creating...' : mode === 'dm' ? 'Open Chat' : 'Create'}
            </Button>
          </ModalFooter>
        </form>
      </ModalContent>
    </Modal>
  );
}
