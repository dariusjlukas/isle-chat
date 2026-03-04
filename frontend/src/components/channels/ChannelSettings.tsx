import { useState } from 'react';
import { Modal, ModalContent, ModalHeader, ModalBody, Tabs, Tab,
         Input, Switch, Select, SelectItem, Button } from '@heroui/react';
import { useChatStore } from '../../stores/chatStore';
import * as api from '../../services/api';
import type { Channel, ChannelMemberInfo, ChannelRole } from '../../types';

interface Props {
  channel: Channel;
  onClose: () => void;
}

export function ChannelSettings({ channel, onClose }: Props) {
  const [name, setName] = useState(channel.name);
  const [description, setDescription] = useState(channel.description);
  const [isPublic, setIsPublic] = useState(channel.is_public);
  const [defaultRole, setDefaultRole] = useState<ChannelRole>(channel.default_role);
  const [saving, setSaving] = useState(false);
  const [inviteUserId, setInviteUserId] = useState('');
  const [inviteRole, setInviteRole] = useState('write');
  const [inviting, setInviting] = useState(false);

  const users = useChatStore((s) => s.users);
  const setChannels = useChatStore((s) => s.setChannels);
  const updateChannel = useChatStore((s) => s.updateChannel);

  const memberIds = new Set(channel.members.map((m) => m.id));
  const nonMembers = users.filter((u) => !memberIds.has(u.id));

  const handleSave = async () => {
    setSaving(true);
    try {
      const updated = await api.updateChannelSettings(channel.id, {
        name, description, is_public: isPublic, default_role: defaultRole,
      });
      updateChannel(updated as any);
    } catch {}
    setSaving(false);
  };

  const handleChangeRole = async (userId: string, newRole: string) => {
    try {
      await api.changeMemberRole(channel.id, userId, newRole);
      const channels = await api.listChannels();
      setChannels(channels as any);
    } catch {}
  };

  const handleKick = async (member: ChannelMemberInfo) => {
    if (!confirm(`Remove ${member.display_name} from #${channel.name}?`)) return;
    try {
      await api.kickFromChannel(channel.id, member.id);
      const channels = await api.listChannels();
      setChannels(channels as any);
    } catch {}
  };

  const handleInvite = async () => {
    if (!inviteUserId) return;
    setInviting(true);
    try {
      await api.inviteToChannel(channel.id, inviteUserId, inviteRole);
      const channels = await api.listChannels();
      setChannels(channels as any);
      setInviteUserId('');
    } catch {}
    setInviting(false);
  };

  return (
    <Modal isOpen onOpenChange={(open) => { if (!open) onClose(); }} size="lg" scrollBehavior="inside" backdrop="opaque">
      <ModalContent>
        <ModalHeader>Channel Settings — #{channel.name}</ModalHeader>
        <ModalBody className="pb-6">
          <Tabs color="primary" classNames={{ tabList: "bg-content2" }}>
            <Tab key="settings" title="Settings">
              <div className="space-y-4 pt-2">
                <Input label="Channel Name" variant="bordered" value={name}
                       onChange={(e) => setName(e.target.value)} />
                <Input label="Description" variant="bordered" value={description}
                       onChange={(e) => setDescription(e.target.value)} />
                <div className="flex items-center justify-between">
                  <div>
                    <p className="text-sm font-medium text-foreground">Public Channel</p>
                    <p className="text-xs text-default-400">
                      {isPublic ? 'Anyone can find and join' : 'Invite only'}
                    </p>
                  </div>
                  <Switch isSelected={isPublic} onValueChange={setIsPublic} size="sm" />
                </div>
                <Select label="Default Role for New Members" variant="bordered"
                        selectedKeys={[defaultRole]}
                        onChange={(e) => setDefaultRole(e.target.value as ChannelRole)}>
                  <SelectItem key="write">Write (can send messages)</SelectItem>
                  <SelectItem key="read">Read Only (can view only)</SelectItem>
                </Select>
                <Button color="primary" onPress={handleSave} isLoading={saving}>
                  Save Settings
                </Button>
              </div>
            </Tab>

            <Tab key="members" title={`Members (${channel.members.length})`}>
              <div className="space-y-2 pt-2">
                {channel.members.map((m) => (
                  <div key={m.id} className="flex items-center justify-between p-2 rounded-lg bg-content1">
                    <div className="flex items-center gap-2 min-w-0">
                      <div className={`w-2 h-2 rounded-full flex-shrink-0 ${m.is_online ? 'bg-success' : 'bg-default-300'}`} />
                      <span className="text-sm truncate">{m.display_name}</span>
                      <span className="text-xs text-default-400">@{m.username}</span>
                    </div>
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
                      <Button size="sm" variant="flat" color="danger" onPress={() => handleKick(m)}>
                        Kick
                      </Button>
                    </div>
                  </div>
                ))}
              </div>
            </Tab>

            <Tab key="invite" title="Invite">
              <div className="space-y-4 pt-2">
                <Select label="Select User" variant="bordered"
                        selectedKeys={inviteUserId ? [inviteUserId] : []}
                        onChange={(e) => setInviteUserId(e.target.value)}>
                  {nonMembers.map((u) => (
                    <SelectItem key={u.id}>
                      {u.display_name} (@{u.username})
                    </SelectItem>
                  ))}
                </Select>
                <Select label="Role" variant="bordered"
                        selectedKeys={[inviteRole]}
                        onChange={(e) => setInviteRole(e.target.value)}>
                  <SelectItem key="admin">Admin</SelectItem>
                  <SelectItem key="write">Write</SelectItem>
                  <SelectItem key="read">Read Only</SelectItem>
                </Select>
                <Button color="primary" onPress={handleInvite} isLoading={inviting}
                        isDisabled={!inviteUserId}>
                  Invite User
                </Button>
                {nonMembers.length === 0 && (
                  <p className="text-sm text-default-400">All users are already members of this channel.</p>
                )}
              </div>
            </Tab>
          </Tabs>
        </ModalBody>
      </ModalContent>
    </Modal>
  );
}
