import { useState, useMemo } from 'react';
import {
  Modal,
  ModalContent,
  ModalHeader,
  ModalBody,
  Tabs,
  Tab,
  Input,
  Switch,
  Select,
  SelectItem,
  Button,
} from '@heroui/react';
import { useChatStore } from '../../stores/chatStore';
import * as api from '../../services/api';
import type { Space, ChannelMemberInfo, ChannelRole } from '../../types';
import { UserPicker } from '../common/UserPicker';
import { OnlineStatusDot } from '../common/OnlineStatusDot';
import { UserPopoverCard } from '../common/UserPopoverCard';

interface Props {
  space: Space;
  onClose: () => void;
}

export function SpaceSettings({ space, onClose }: Props) {
  const [name, setName] = useState(space.name);
  const [description, setDescription] = useState(space.description);
  const [icon, setIcon] = useState(space.icon);
  const [isPublic, setIsPublic] = useState(space.is_public);
  const [defaultRole, setDefaultRole] = useState<ChannelRole>(
    space.default_role,
  );
  const [saving, setSaving] = useState(false);
  const [inviteUserId, setInviteUserId] = useState<string[]>([]);
  const [inviteRole, setInviteRole] = useState('write');
  const [inviting, setInviting] = useState(false);

  const setSpaces = useChatStore((s) => s.setSpaces);
  const updateSpace = useChatStore((s) => s.updateSpace);

  const memberIds = useMemo(
    () => space.members.map((m) => m.id),
    [space.members],
  );

  const handleSave = async () => {
    setSaving(true);
    try {
      const updated = await api.updateSpaceSettings(space.id, {
        name,
        description,
        icon,
        is_public: isPublic,
        default_role: defaultRole,
      });
      updateSpace({ id: space.id, ...updated });
    } catch {
      /* ignored */
    }
    setSaving(false);
  };

  const handleChangeRole = async (userId: string, newRole: string) => {
    try {
      await api.changeSpaceMemberRole(space.id, userId, newRole);
      const spaces = await api.listSpaces();
      setSpaces(spaces);
    } catch {
      /* ignored */
    }
  };

  const handleKick = async (member: ChannelMemberInfo) => {
    if (!confirm(`Remove ${member.display_name} from ${space.name}?`)) return;
    try {
      await api.kickFromSpace(space.id, member.id);
      const spaces = await api.listSpaces();
      setSpaces(spaces);
    } catch {
      /* ignored */
    }
  };

  const handleInvite = async () => {
    if (inviteUserId.length === 0) return;
    setInviting(true);
    try {
      await api.inviteToSpace(space.id, inviteUserId[0], inviteRole);
      const spaces = await api.listSpaces();
      setSpaces(spaces);
      setInviteUserId([]);
    } catch {
      /* ignored */
    }
    setInviting(false);
  };

  return (
    <Modal
      isOpen
      onOpenChange={(open) => {
        if (!open) onClose();
      }}
      size="lg"
      scrollBehavior="inside"
      backdrop="opaque"
    >
      <ModalContent>
        <ModalHeader>
          Space Settings —{' '}
          {space.icon && <span className="mr-1">{space.icon}</span>}
          {space.name}
        </ModalHeader>
        <ModalBody className="pb-6">
          <Tabs color="primary" classNames={{ tabList: 'bg-content2' }}>
            <Tab key="settings" title="Settings">
              <div className="space-y-4 pt-2">
                <Input
                  label="Space Name"
                  variant="bordered"
                  value={name}
                  onChange={(e) => setName(e.target.value)}
                />
                <Input
                  label="Description"
                  variant="bordered"
                  value={description}
                  onChange={(e) => setDescription(e.target.value)}
                />
                <Input
                  label="Icon"
                  description="Emoji or short text"
                  variant="bordered"
                  value={icon}
                  onChange={(e) => setIcon(e.target.value)}
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
                  onChange={(e) =>
                    setDefaultRole(e.target.value as ChannelRole)
                  }
                >
                  <SelectItem key="write">Write (can send messages)</SelectItem>
                  <SelectItem key="read">Read Only (can view only)</SelectItem>
                </Select>
                <Button color="primary" onPress={handleSave} isLoading={saving}>
                  Save Settings
                </Button>
              </div>
            </Tab>

            <Tab key="members" title={`Members (${space.members.length})`}>
              <div className="space-y-2 pt-2">
                {space.members.map((m) => (
                  <div
                    key={m.id}
                    className="flex items-center justify-between p-2 rounded-lg bg-content1"
                  >
                    <UserPopoverCard userId={m.id}>
                      <div className="flex items-center gap-2 min-w-0 cursor-pointer">
                        <OnlineStatusDot
                          isOnline={m.is_online}
                          lastSeen={m.last_seen}
                        />
                        <span className="text-sm truncate hover:underline">
                          {m.display_name}
                        </span>
                        <span className="text-xs text-default-400">
                          @{m.username}
                        </span>
                      </div>
                    </UserPopoverCard>
                    <div className="flex items-center gap-2 flex-shrink-0">
                      <Select
                        size="sm"
                        variant="bordered"
                        className="w-28"
                        selectedKeys={[m.role]}
                        onChange={(e) => handleChangeRole(m.id, e.target.value)}
                        aria-label="Role"
                      >
                        <SelectItem key="admin">Admin</SelectItem>
                        <SelectItem key="write">Write</SelectItem>
                        <SelectItem key="read">Read</SelectItem>
                      </Select>
                      <Button
                        size="sm"
                        variant="flat"
                        color="danger"
                        onPress={() => handleKick(m)}
                      >
                        Kick
                      </Button>
                    </div>
                  </div>
                ))}
              </div>
            </Tab>

            <Tab key="invite" title="Invite">
              <div className="space-y-4 pt-2">
                <UserPicker
                  mode="single"
                  selected={inviteUserId}
                  onChange={setInviteUserId}
                  excludeIds={memberIds}
                  label="Select user"
                  placeholder="Search users..."
                />
                <Select
                  label="Role"
                  variant="bordered"
                  selectedKeys={[inviteRole]}
                  onChange={(e) => setInviteRole(e.target.value)}
                >
                  <SelectItem key="admin">Admin</SelectItem>
                  <SelectItem key="write">Write</SelectItem>
                  <SelectItem key="read">Read Only</SelectItem>
                </Select>
                <Button
                  color="primary"
                  onPress={handleInvite}
                  isLoading={inviting}
                  isDisabled={inviteUserId.length === 0}
                >
                  Invite User
                </Button>
              </div>
            </Tab>
          </Tabs>
        </ModalBody>
      </ModalContent>
    </Modal>
  );
}
