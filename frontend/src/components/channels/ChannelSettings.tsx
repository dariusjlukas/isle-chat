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
import type { Channel, ChannelMemberInfo, ChannelRole } from '../../types';
import { UserPicker } from '../common/UserPicker';
import { OnlineStatusDot } from '../common/OnlineStatusDot';
import { UserPopoverCard } from '../common/UserPopoverCard';

interface Props {
  channel: Channel;
  onClose: () => void;
}

export function ChannelSettings({ channel, onClose }: Props) {
  const [name, setName] = useState(channel.name);
  const [description, setDescription] = useState(channel.description);
  const [isPublic, setIsPublic] = useState(channel.is_public);
  const [defaultRole, setDefaultRole] = useState<ChannelRole>(
    channel.default_role,
  );
  const [saving, setSaving] = useState(false);
  const [inviteUserId, setInviteUserId] = useState<string[]>([]);
  const [inviteRole, setInviteRole] = useState('write');
  const [inviting, setInviting] = useState(false);

  const [leaveError, setLeaveError] = useState<string | null>(null);
  const user = useChatStore((s) => s.user);
  const setChannels = useChatStore((s) => s.setChannels);
  const updateChannel = useChatStore((s) => s.updateChannel);
  const removeChannel = useChatStore((s) => s.removeChannel);

  const canManage =
    channel.my_role === 'admin' ||
    channel.my_role === 'owner' ||
    user?.role === 'admin' ||
    user?.role === 'owner';

  const memberIds = useMemo(
    () => channel.members.map((m) => m.id),
    [channel.members],
  );

  const handleSave = async () => {
    setSaving(true);
    try {
      const updated = await api.updateChannelSettings(channel.id, {
        name,
        description,
        is_public: isPublic,
        default_role: defaultRole,
      });
      updateChannel(updated);
    } catch (e) {
      console.error('Channel operation failed:', e);
    }
    setSaving(false);
  };

  const handleChangeRole = async (userId: string, newRole: string) => {
    try {
      await api.changeMemberRole(channel.id, userId, newRole);
      const channels = await api.listChannels();
      setChannels(channels);
    } catch (e) {
      console.error('Channel operation failed:', e);
    }
  };

  const handleKick = async (member: ChannelMemberInfo) => {
    if (!confirm(`Remove ${member.display_name} from #${channel.name}?`))
      return;
    try {
      await api.kickFromChannel(channel.id, member.id);
      const channels = await api.listChannels();
      setChannels(channels);
    } catch (e) {
      console.error('Channel operation failed:', e);
    }
  };

  const handleInvite = async () => {
    if (inviteUserId.length === 0) return;
    setInviting(true);
    try {
      await api.inviteToChannel(channel.id, inviteUserId[0], inviteRole);
      const channels = await api.listChannels();
      setChannels(channels);
      setInviteUserId([]);
    } catch (e) {
      console.error('Channel operation failed:', e);
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
        <ModalHeader>Channel Settings — #{channel.name}</ModalHeader>
        <ModalBody className='pb-6'>
          <Tabs color='primary' classNames={{ tabList: 'bg-content2' }}>
            {canManage && (
              <Tab key='settings' title='Settings'>
                <div className='space-y-4 pt-2'>
                  <Input
                    label='Channel Name'
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

            <Tab key='members' title={`Members (${channel.members.length})`}>
              <div className='space-y-2 pt-2'>
                {channel.members.map((m) => (
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
                    {canManage ? (
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
                        >
                          <SelectItem key='admin'>Admin</SelectItem>
                          <SelectItem key='write'>Write</SelectItem>
                          <SelectItem key='read'>Read</SelectItem>
                        </Select>
                        <Button
                          size='sm'
                          variant='flat'
                          color='danger'
                          onPress={() => handleKick(m)}
                        >
                          Kick
                        </Button>
                      </div>
                    ) : (
                      <span className='text-xs text-default-400 flex-shrink-0'>
                        {m.role}
                      </span>
                    )}
                  </div>
                ))}
              </div>
            </Tab>

            {canManage && (
              <Tab key='invite' title='Add Member'>
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
                    Add User
                  </Button>
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
                    await api.leaveChannel(channel.id);
                    removeChannel(channel.id);
                    onClose();
                  } catch (e) {
                    const msg = e instanceof Error ? e.message : 'Failed';
                    setLeaveError(msg);
                  }
                }}
              >
                Leave Channel
              </Button>
              {canManage && !channel.is_archived && (
                <Button
                  variant='flat'
                  color='danger'
                  onPress={async () => {
                    if (
                      !confirm(
                        `Archive #${channel.name}? No new messages can be sent.`,
                      )
                    )
                      return;
                    try {
                      await api.archiveChannel(channel.id);
                      updateChannel({
                        id: channel.id,
                        is_archived: true,
                      });
                    } catch (e) {
                      console.error('Channel archive failed:', e);
                    }
                  }}
                >
                  Archive Channel
                </Button>
              )}
              {canManage && channel.is_archived && (
                <Button
                  variant='flat'
                  color='success'
                  onPress={async () => {
                    try {
                      await api.unarchiveChannel(channel.id);
                      updateChannel({
                        id: channel.id,
                        is_archived: false,
                      });
                    } catch (e) {
                      const msg = e instanceof Error ? e.message : 'Failed';
                      setLeaveError(msg);
                    }
                  }}
                >
                  Unarchive Channel
                </Button>
              )}
            </div>
          </div>
        </ModalBody>
      </ModalContent>
    </Modal>
  );
}
