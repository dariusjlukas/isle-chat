import { useState } from 'react';
import {
  Modal,
  ModalContent,
  ModalHeader,
  ModalBody,
  ModalFooter,
  Button,
  Input,
  Switch,
  Select,
  SelectItem,
  Alert,
} from '@heroui/react';
import * as api from '../../services/api';
import { useChatStore } from '../../stores/chatStore';

interface Props {
  onClose: () => void;
}

export function CreateSpace({ onClose }: Props) {
  const [name, setName] = useState('');
  const [description, setDescription] = useState('');
  const [icon, setIcon] = useState('');
  const [isPublic, setIsPublic] = useState(true);
  const [defaultRole, setDefaultRole] = useState('write');
  const [error, setError] = useState('');
  const [loading, setLoading] = useState(false);
  const setActiveView = useChatStore((s) => s.setActiveView);

  const handleSubmit = async (e: React.FormEvent) => {
    e.preventDefault();
    if (!name.trim()) {
      setError('Space name is required');
      return;
    }
    setLoading(true);
    setError('');
    try {
      const space = await api.createSpace(
        name.trim(),
        description.trim(),
        icon.trim(),
        isPublic,
        defaultRole,
      );
      const spaces = await api.listSpaces();
      useChatStore.getState().setSpaces(spaces);
      setActiveView({ type: 'space', spaceId: space.id });
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
      size="md"
      backdrop="opaque"
    >
      <ModalContent>
        <form onSubmit={handleSubmit}>
          <ModalHeader>Create Space</ModalHeader>
          <ModalBody>
            {error && (
              <Alert color="danger" variant="flat">
                {error}
              </Alert>
            )}
            <Input
              label="Space Name"
              variant="bordered"
              value={name}
              onChange={(e) => setName(e.target.value)}
              placeholder="e.g. Engineering"
            />
            <Input
              label="Description"
              description="Optional"
              variant="bordered"
              value={description}
              onChange={(e) => setDescription(e.target.value)}
              placeholder="What's this space for?"
            />
            <Input
              label="Icon"
              description="Emoji or short text"
              variant="bordered"
              value={icon}
              onChange={(e) => setIcon(e.target.value)}
              placeholder="e.g. 🚀"
              maxLength={10}
            />
            <div className="flex items-center justify-between">
              <div>
                <p className="text-sm font-medium text-foreground">
                  Public Space
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
          </ModalBody>
          <ModalFooter>
            <Button variant="light" color="default" onPress={onClose}>
              Cancel
            </Button>
            <Button type="submit" color="primary" isLoading={loading}>
              Create Space
            </Button>
          </ModalFooter>
        </form>
      </ModalContent>
    </Modal>
  );
}
