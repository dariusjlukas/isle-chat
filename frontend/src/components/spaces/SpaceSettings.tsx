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
  const [inviteSent, setInviteSent] = useState(false);
  const [inviteError, setInviteError] = useState<string | null>(null);

  const [leaveError, setLeaveError] = useState<string | null>(null);
  const user = useChatStore((s) => s.user);
  const setSpaces = useChatStore((s) => s.setSpaces);
  const updateSpace = useChatStore((s) => s.updateSpace);
  const removeSpace = useChatStore((s) => s.removeSpace);

  const SPACE_RANK: Record<string, number> = {
    owner: 3,
    admin: 2,
    write: 1,
    read: 0,
  };

  // Actor's effective rank is the higher of space role and server role
  const spaceRoleRank = SPACE_RANK[space.my_role] ?? 0;
  const serverRoleRank =
    user?.role === 'owner' ? 3 : user?.role === 'admin' ? 2 : 0;
  const actorRank = Math.max(spaceRoleRank, serverRoleRank);

  const canManage = actorRank >= SPACE_RANK['admin'];

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
    } catch (e) {
      console.error('Space operation failed:', e);
    }
    setSaving(false);
  };

  const handleChangeRole = async (userId: string, newRole: string) => {
    try {
      await api.changeSpaceMemberRole(space.id, userId, newRole);
      const spaces = await api.listSpaces();
      setSpaces(spaces);
    } catch (e) {
      console.error('Space operation failed:', e);
    }
  };

  const handleKick = async (member: ChannelMemberInfo) => {
    if (!confirm(`Remove ${member.display_name} from ${space.name}?`)) return;
    try {
      await api.kickFromSpace(space.id, member.id);
      const spaces = await api.listSpaces();
      setSpaces(spaces);
    } catch (e) {
      console.error('Space operation failed:', e);
    }
  };

  const handleInvite = async () => {
    if (inviteUserId.length === 0) return;
    setInviting(true);
    setInviteSent(false);
    setInviteError(null);
    try {
      await api.inviteToSpace(space.id, inviteUserId[0], inviteRole);
      setInviteUserId([]);
      setInviteSent(true);
      setTimeout(() => setInviteSent(false), 3000);
    } catch (e) {
      const msg = e instanceof Error ? e.message : 'Failed to send invite';
      setInviteError(msg);
    }
    setInviting(false);
  };

  return (
    <Modal
      isOpen
      onOpenChange={(open) => {
        if (!open) onClose();
      }}
      size='3xl'
      scrollBehavior='inside'
      backdrop='opaque'
    >
      <ModalContent>
        <ModalHeader>
          Space Settings —{' '}
          {space.icon && <span className='mr-1'>{space.icon}</span>}
          {space.name}
        </ModalHeader>
        <ModalBody className='pb-6'>
          <Tabs color='primary' classNames={{ tabList: 'bg-content2' }}>
            {canManage && (
              <Tab key='settings' title='Settings'>
                <div className='space-y-4 pt-2'>
                  <Input
                    label='Space Name'
                    variant='bordered'
                    value={name}
                    onChange={(e) => setName(e.target.value)}
                  />
                  <Input
                    label='Description'
                    variant='bordered'
                    value={description}
                    onChange={(e) => setDescription(e.target.value)}
                  />
                  <Input
                    label='Icon'
                    description='Emoji or short text'
                    variant='bordered'
                    value={icon}
                    onChange={(e) => setIcon(e.target.value)}
                    maxLength={10}
                  />
                  <div className='flex items-center justify-between'>
                    <div>
                      <p className='text-sm font-medium text-foreground'>
                        Public Space
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
                    onChange={(e) =>
                      setDefaultRole(e.target.value as ChannelRole)
                    }
                  >
                    <SelectItem key='write'>
                      Write (can send messages)
                    </SelectItem>
                    <SelectItem key='read'>
                      Read Only (can view only)
                    </SelectItem>
                  </Select>
                  <Button
                    color='primary'
                    onPress={handleSave}
                    isLoading={saving}
                  >
                    Save Settings
                  </Button>
                </div>
              </Tab>
            )}

            <Tab key='members' title={`Members (${space.members.length})`}>
              <div className='space-y-2 pt-2'>
                {space.members.map((m) => (
                  <div
                    key={m.id}
                    className='flex items-center justify-between p-2 rounded-lg bg-content1'
                  >
                    <UserPopoverCard userId={m.id}>
                      <div className='flex items-center gap-2 min-w-0 cursor-pointer'>
                        <OnlineStatusDot
                          isOnline={m.is_online}
                          lastSeen={m.last_seen}
                        />
                        <span className='text-sm truncate hover:underline'>
                          {m.display_name}
                        </span>
                        <span className='text-xs text-default-400'>
                          @{m.username}
                        </span>
                      </div>
                    </UserPopoverCard>
                    {(() => {
                      const targetRank = SPACE_RANK[m.role] ?? 0;
                      const isSelf = m.id === user?.id;
                      const canEditMember =
                        canManage && (targetRank < actorRank || isSelf);
                      const roleItems = [
                        { key: 'owner', label: 'Owner', rank: 3 },
                        { key: 'admin', label: 'Admin', rank: 2 },
                        { key: 'write', label: 'Write', rank: 1 },
                        { key: 'read', label: 'Read', rank: 0 },
                      ].filter((r) => r.rank <= actorRank);
                      return canEditMember ? (
                        <div className='flex items-center gap-2 flex-shrink-0'>
                          <Select
                            size='sm'
                            variant='bordered'
                            className='w-28'
                            selectedKeys={[m.role]}
                            onChange={(e) =>
                              handleChangeRole(m.id, e.target.value)
                            }
                            aria-label='Role'
                            items={roleItems}
                          >
                            {(item) => (
                              <SelectItem key={item.key}>
                                {item.label}
                              </SelectItem>
                            )}
                          </Select>
                          {!isSelf && (
                            <Button
                              size='sm'
                              variant='flat'
                              color='danger'
                              onPress={() => handleKick(m)}
                            >
                              Kick
                            </Button>
                          )}
                        </div>
                      ) : (
                        <span className='text-xs text-default-400 flex-shrink-0 capitalize'>
                          {m.role}
                        </span>
                      );
                    })()}
                  </div>
                ))}
              </div>
            </Tab>

            {canManage && (
              <Tab key='invite' title='Invite'>
                <div className='space-y-4 pt-2'>
                  <UserPicker
                    mode='single'
                    selected={inviteUserId}
                    onChange={setInviteUserId}
                    excludeIds={memberIds}
                    label='Select user'
                    placeholder='Search users...'
                  />
                  <Select
                    label='Role'
                    variant='bordered'
                    selectedKeys={[inviteRole]}
                    onChange={(e) => setInviteRole(e.target.value)}
                  >
                    <SelectItem key='admin'>Admin</SelectItem>
                    <SelectItem key='write'>Write</SelectItem>
                    <SelectItem key='read'>Read Only</SelectItem>
                  </Select>
                  <Button
                    color='primary'
                    onPress={handleInvite}
                    isLoading={inviting}
                    isDisabled={inviteUserId.length === 0}
                  >
                    Send Invite
                  </Button>
                  {inviteSent && (
                    <p className='text-xs text-success'>
                      Invite sent successfully!
                    </p>
                  )}
                  {inviteError && (
                    <p className='text-xs text-danger'>{inviteError}</p>
                  )}
                </div>
              </Tab>
            )}
          </Tabs>

          <div className='border-t border-divider pt-4 mt-2 space-y-3'>
            {leaveError && <p className='text-xs text-danger'>{leaveError}</p>}
            <div className='flex gap-2'>
              <Button
                variant='flat'
                color='warning'
                onPress={async () => {
                  try {
                    setLeaveError(null);
                    await api.leaveSpace(space.id);
                    removeSpace(space.id);
                    onClose();
                  } catch (e) {
                    const msg = e instanceof Error ? e.message : 'Failed';
                    setLeaveError(msg);
                  }
                }}
              >
                Leave Space
              </Button>
              {canManage && !space.is_archived && (
                <Button
                  variant='flat'
                  color='danger'
                  onPress={async () => {
                    if (
                      !confirm(
                        `Archive ${space.name}? All channels will be archived.`,
                      )
                    )
                      return;
                    try {
                      await api.archiveSpace(space.id);
                      updateSpace({
                        id: space.id,
                        is_archived: true,
                      });
                    } catch (e) {
                      console.error('Space archive failed:', e);
                    }
                  }}
                >
                  Archive Space
                </Button>
              )}
              {canManage && space.is_archived && (
                <Button
                  variant='flat'
                  color='success'
                  onPress={async () => {
                    try {
                      await api.unarchiveSpace(space.id);
                      updateSpace({
                        id: space.id,
                        is_archived: false,
                      });
                    } catch (e) {
                      const msg = e instanceof Error ? e.message : 'Failed';
                      setLeaveError(msg);
                    }
                  }}
                >
                  Unarchive Space
                </Button>
              )}
            </div>
          </div>
        </ModalBody>
      </ModalContent>
    </Modal>
  );
}
