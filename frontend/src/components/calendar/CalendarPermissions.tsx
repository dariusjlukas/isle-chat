import { useState, useEffect } from 'react';
import {
  Modal,
  ModalContent,
  ModalHeader,
  ModalBody,
  ModalFooter,
  Button,
  Select,
  SelectItem,
} from '@heroui/react';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import { faUserPlus, faUserMinus } from '@fortawesome/free-solid-svg-icons';
import * as api from '../../services/api';
import type { CalendarPermission } from '../../types';
import { useChatStore } from '../../stores/chatStore';

interface Props {
  spaceId: string;
  onClose: () => void;
}

export function CalendarPermissions({ spaceId, onClose }: Props) {
  const spaces = useChatStore((s) => s.spaces);
  const space = spaces.find((s) => s.id === spaceId);

  const [permissions, setPermissions] = useState<CalendarPermission[]>([]);
  const [loading, setLoading] = useState(true);

  // Add permission form
  const [showAdd, setShowAdd] = useState(false);
  const [selectedUserId, setSelectedUserId] = useState('');
  const [selectedPerm, setSelectedPerm] = useState('edit');

  const loadPermissions = async () => {
    try {
      const data = await api.getCalendarPermissions(spaceId);
      setPermissions(data.permissions);
    } catch {
      // error
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => {
    loadPermissions();
  }, [spaceId]);

  const handleAdd = async () => {
    if (!selectedUserId) return;
    try {
      await api.setCalendarPermission(spaceId, selectedUserId, selectedPerm);
      setShowAdd(false);
      setSelectedUserId('');
      loadPermissions();
    } catch {
      // error
    }
  };

  const handleRemove = async (userId: string) => {
    try {
      await api.removeCalendarPermission(spaceId, userId);
      loadPermissions();
    } catch {
      // error
    }
  };

  const handleChange = async (userId: string, permission: string) => {
    try {
      await api.setCalendarPermission(spaceId, userId, permission);
      loadPermissions();
    } catch {
      // error
    }
  };

  // Members who don't already have explicit permissions
  const availableUsers = (space?.members || []).filter(
    (m) => !permissions.some((p) => p.user_id === m.id),
  );

  return (
    <Modal isOpen onClose={onClose} size='lg' scrollBehavior='inside'>
      <ModalContent>
        <ModalHeader>Calendar Permissions</ModalHeader>
        <ModalBody>
          <p className='text-sm text-default-500 mb-3'>
            Permissions inherit from space roles. Overrides here can only
            increase access (e.g., grant &quot;edit&quot; to a &quot;read&quot;
            member). Space admins and owners always have full access.
          </p>

          {/* Existing permissions */}
          <div className='space-y-2'>
            {permissions.map((p) => (
              <div
                key={p.id}
                className='flex items-center justify-between p-2 rounded-lg bg-content2/50'
              >
                <div>
                  <span className='text-sm font-medium'>
                    {p.display_name || p.username}
                  </span>
                  <span className='text-xs text-default-400 ml-2'>
                    @{p.username}
                  </span>
                </div>
                <div className='flex items-center gap-2'>
                  <Select
                    selectedKeys={[p.permission]}
                    onSelectionChange={(keys) => {
                      const perm = Array.from(keys)[0] as string;
                      handleChange(p.user_id, perm);
                    }}
                    size='sm'
                    className='w-28'
                  >
                    <SelectItem key='view'>View</SelectItem>
                    <SelectItem key='edit'>Edit</SelectItem>
                    <SelectItem key='owner'>Owner</SelectItem>
                  </Select>
                  <Button
                    isIconOnly
                    variant='light'
                    size='sm'
                    color='danger'
                    onPress={() => handleRemove(p.user_id)}
                    title='Remove permission'
                  >
                    <FontAwesomeIcon icon={faUserMinus} />
                  </Button>
                </div>
              </div>
            ))}
            {permissions.length === 0 && !loading && (
              <p className='text-center text-default-400 text-sm py-4'>
                No explicit permission overrides
              </p>
            )}
          </div>

          {/* Add permission */}
          {showAdd ? (
            <div className='flex items-end gap-2 mt-3'>
              <Select
                label='User'
                selectedKeys={selectedUserId ? [selectedUserId] : []}
                onSelectionChange={(keys) =>
                  setSelectedUserId(Array.from(keys)[0] as string)
                }
                className='flex-1'
                size='sm'
              >
                {availableUsers.map((u) => (
                  <SelectItem key={u.id}>
                    {u.display_name || u.username}
                  </SelectItem>
                ))}
              </Select>
              <Select
                label='Permission'
                selectedKeys={[selectedPerm]}
                onSelectionChange={(keys) =>
                  setSelectedPerm(Array.from(keys)[0] as string)
                }
                className='w-28'
                size='sm'
              >
                <SelectItem key='view'>View</SelectItem>
                <SelectItem key='edit'>Edit</SelectItem>
                <SelectItem key='owner'>Owner</SelectItem>
              </Select>
              <Button color='primary' size='sm' onPress={handleAdd}>
                Add
              </Button>
              <Button
                variant='light'
                size='sm'
                onPress={() => setShowAdd(false)}
              >
                Cancel
              </Button>
            </div>
          ) : (
            <Button
              variant='flat'
              size='sm'
              onPress={() => setShowAdd(true)}
              startContent={<FontAwesomeIcon icon={faUserPlus} />}
              className='mt-3'
            >
              Add Permission Override
            </Button>
          )}
        </ModalBody>
        <ModalFooter>
          <Button onPress={onClose}>Close</Button>
        </ModalFooter>
      </ModalContent>
    </Modal>
  );
}
