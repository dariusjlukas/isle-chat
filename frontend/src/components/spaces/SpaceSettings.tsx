import { useState, useMemo, useRef, useCallback } from 'react';
import {
  Modal,
  ModalContent,
  ModalHeader,
  ModalBody,
  ModalFooter,
  Tabs,
  Tab,
  Input,
  Switch,
  Select,
  SelectItem,
  Button,
  Slider,
} from '@heroui/react';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import { faCamera, faTrashCan } from '@fortawesome/free-solid-svg-icons';
import Cropper from 'react-easy-crop';
import type { Area } from 'react-easy-crop';
import { useChatStore } from '../../stores/chatStore';
import * as api from '../../services/api';
import type { Space, ChannelMemberInfo, ChannelRole } from '../../types';
import { UserPicker } from '../common/UserPicker';
import { OnlineStatusDot } from '../common/OnlineStatusDot';
import { UserPopoverCard } from '../common/UserPopoverCard';
import { SpaceAvatar } from '../common/SpaceAvatar';

async function getCroppedBlob(imageSrc: string, crop: Area): Promise<Blob> {
  const image = new Image();
  image.crossOrigin = 'anonymous';
  await new Promise<void>((resolve, reject) => {
    image.onload = () => resolve();
    image.onerror = reject;
    image.src = imageSrc;
  });

  const canvas = document.createElement('canvas');
  const size = Math.min(crop.width, crop.height);
  const outputSize = Math.min(size, 512);
  canvas.width = outputSize;
  canvas.height = outputSize;

  const ctx = canvas.getContext('2d')!;
  ctx.drawImage(
    image,
    crop.x,
    crop.y,
    crop.width,
    crop.height,
    0,
    0,
    outputSize,
    outputSize,
  );

  return new Promise((resolve, reject) => {
    canvas.toBlob(
      (blob) => (blob ? resolve(blob) : reject(new Error('Failed to crop'))),
      'image/png',
    );
  });
}

interface Props {
  space: Space;
  onClose: () => void;
}

export function SpaceSettings({ space, onClose }: Props) {
  const [name, setName] = useState(space.name);
  const [description, setDescription] = useState(space.description);
  const [isPublic, setIsPublic] = useState(space.is_public);
  const [defaultRole, setDefaultRole] = useState<ChannelRole>(
    space.default_role,
  );
  const [profileColor, setProfileColor] = useState(space.profile_color || '');
  const [saving, setSaving] = useState(false);
  const [inviteUserId, setInviteUserId] = useState<string[]>([]);
  const [inviteRole, setInviteRole] = useState('write');
  const [inviting, setInviting] = useState(false);
  const [inviteSent, setInviteSent] = useState(false);
  const [inviteError, setInviteError] = useState<string | null>(null);

  const [uploading, setUploading] = useState(false);
  const [avatarMsg, setAvatarMsg] = useState('');
  const fileInputRef = useRef<HTMLInputElement>(null);

  const [savedPayload, setSavedPayload] = useState(() =>
    JSON.stringify({
      name,
      description,
      isPublic,
      defaultRole,
      profileColor,
    }),
  );

  const currentPayload = useMemo(
    () =>
      JSON.stringify({
        name,
        description,
        isPublic,
        defaultRole,
        profileColor,
      }),
    [name, description, isPublic, defaultRole, profileColor],
  );

  const isDirty = currentPayload !== savedPayload;

  // Crop state
  const [cropImage, setCropImage] = useState<string | null>(null);
  const [crop, setCrop] = useState({ x: 0, y: 0 });
  const [zoom, setZoom] = useState(1);
  const [croppedArea, setCroppedArea] = useState<Area | null>(null);

  const onCropComplete = useCallback((_: Area, croppedPixels: Area) => {
    setCroppedArea(croppedPixels);
  }, []);

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
        is_public: isPublic,
        default_role: defaultRole,
        profile_color: profileColor,
      });
      updateSpace({ id: space.id, ...updated });
      setSavedPayload(
        JSON.stringify({
          name,
          description,
          isPublic,
          defaultRole,
          profileColor,
        }),
      );
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

  const handleFileSelect = (e: React.ChangeEvent<HTMLInputElement>) => {
    const file = e.target.files?.[0];
    if (!file) return;

    if (!file.type.startsWith('image/')) {
      setAvatarMsg('Please select an image file');
      setTimeout(() => setAvatarMsg(''), 3000);
      return;
    }

    const reader = new FileReader();
    reader.onload = () => {
      setCropImage(reader.result as string);
      setCrop({ x: 0, y: 0 });
      setZoom(1);
    };
    reader.readAsDataURL(file);

    if (fileInputRef.current) fileInputRef.current.value = '';
  };

  const handleCropConfirm = async () => {
    if (!cropImage || !croppedArea) return;

    setUploading(true);
    try {
      const blob = await getCroppedBlob(cropImage, croppedArea);
      const file = new File([blob], 'space-avatar.png', { type: 'image/png' });
      const updated = await api.uploadSpaceAvatar(space.id, file);
      updateSpace({ id: space.id, ...updated });
    } catch (e) {
      setAvatarMsg(e instanceof Error ? e.message : 'Upload failed');
      setTimeout(() => setAvatarMsg(''), 3000);
    } finally {
      setUploading(false);
      setCropImage(null);
    }
  };

  const handleCropCancel = () => {
    setCropImage(null);
  };

  const handleRemoveAvatar = async () => {
    setUploading(true);
    try {
      const updated = await api.deleteSpaceAvatar(space.id);
      updateSpace({ id: space.id, ...updated });
    } catch (e) {
      setAvatarMsg(e instanceof Error ? e.message : 'Failed to remove');
      setTimeout(() => setAvatarMsg(''), 3000);
    } finally {
      setUploading(false);
    }
  };

  return (
    <>
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
            <div className='flex items-center gap-3'>
              <span>Space Settings — {space.name}</span>
              {isDirty && (
                <span className='text-xs font-normal text-warning bg-warning/10 px-2 py-0.5 rounded-full'>
                  Unsaved changes
                </span>
              )}
            </div>
          </ModalHeader>
          <ModalBody className='pb-6'>
            <Tabs color='primary' classNames={{ tabList: 'bg-content2' }}>
              {canManage && (
                <Tab key='settings' title='Settings'>
                  <div className='space-y-4 pt-2'>
                    {/* Space avatar */}
                    <div className='flex items-center gap-4'>
                      <div className='relative group'>
                        <SpaceAvatar
                          name={space.name}
                          avatarFileId={space.avatar_file_id}
                          profileColor={profileColor || space.profile_color}
                          size='lg'
                        />
                        <button
                          type='button'
                          className='absolute inset-0 rounded-xl bg-black/50 flex items-center justify-center opacity-0 group-hover:opacity-100 transition-opacity cursor-pointer'
                          onClick={() => fileInputRef.current?.click()}
                          disabled={uploading}
                        >
                          <FontAwesomeIcon
                            icon={faCamera}
                            className='text-white text-lg'
                          />
                        </button>
                        <input
                          ref={fileInputRef}
                          type='file'
                          accept='image/*'
                          className='hidden'
                          onChange={handleFileSelect}
                        />
                      </div>
                      <div className='flex flex-col gap-1'>
                        <Button
                          size='sm'
                          variant='flat'
                          onPress={() => fileInputRef.current?.click()}
                          isLoading={uploading}
                        >
                          {uploading ? 'Uploading...' : 'Change Picture'}
                        </Button>
                        {space.avatar_file_id && (
                          <Button
                            size='sm'
                            variant='light'
                            color='danger'
                            onPress={handleRemoveAvatar}
                            isLoading={uploading}
                            startContent={
                              <FontAwesomeIcon
                                icon={faTrashCan}
                                className='text-xs'
                              />
                            }
                          >
                            Remove
                          </Button>
                        )}
                        {avatarMsg && (
                          <span className='text-xs text-danger'>
                            {avatarMsg}
                          </span>
                        )}
                      </div>
                    </div>
                    {/* Background color picker (for default avatar) */}
                    <div>
                      <p className='text-sm text-default-600 mb-2'>
                        Background Color
                      </p>
                      <div className='flex flex-wrap gap-2'>
                        {[
                          '#e53e3e',
                          '#dd6b20',
                          '#d69e2e',
                          '#38a169',
                          '#319795',
                          '#3182ce',
                          '#5a67d8',
                          '#805ad5',
                          '#d53f8c',
                          '#718096',
                        ].map((color) => (
                          <button
                            key={color}
                            type='button'
                            className={`w-7 h-7 rounded-lg border-2 transition-all ${
                              profileColor === color
                                ? 'border-foreground scale-110'
                                : 'border-transparent hover:scale-105'
                            }`}
                            style={{ backgroundColor: color }}
                            onClick={() => setProfileColor(color)}
                          />
                        ))}
                        {profileColor && (
                          <button
                            type='button'
                            className='text-xs text-default-400 hover:text-foreground px-2'
                            onClick={() => setProfileColor('')}
                          >
                            Reset
                          </button>
                        )}
                      </div>
                    </div>
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
                    <div className='flex items-center justify-between'>
                      <div>
                        <p className='text-sm font-medium text-foreground'>
                          Public Space
                        </p>
                        <p className='text-xs text-default-400'>
                          {isPublic
                            ? 'Anyone can find and join'
                            : 'Invite only'}
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
                      color={isDirty ? 'warning' : 'primary'}
                      onPress={handleSave}
                      isLoading={saving}
                    >
                      {isDirty
                        ? 'Save Settings (unsaved changes)'
                        : 'Save Settings'}
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
              {leaveError && (
                <p className='text-xs text-danger'>{leaveError}</p>
              )}
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

      {/* Crop Modal */}
      <Modal
        isOpen={!!cropImage}
        onOpenChange={(open) => !open && handleCropCancel()}
        size='lg'
        backdrop='opaque'
      >
        <ModalContent>
          <ModalHeader>Crop Space Picture</ModalHeader>
          <ModalBody>
            <div className='relative w-full' style={{ height: 350 }}>
              {cropImage && (
                <Cropper
                  image={cropImage}
                  crop={crop}
                  zoom={zoom}
                  aspect={1}
                  cropShape='rect'
                  showGrid={false}
                  onCropChange={setCrop}
                  onZoomChange={setZoom}
                  onCropComplete={onCropComplete}
                />
              )}
            </div>
            <div className='flex items-center gap-3 px-2'>
              <span className='text-xs text-default-500 whitespace-nowrap'>
                Zoom
              </span>
              <Slider
                size='sm'
                step={0.1}
                minValue={1}
                maxValue={3}
                value={zoom}
                onChange={(v) => setZoom(v as number)}
                className='flex-1'
              />
            </div>
          </ModalBody>
          <ModalFooter>
            <Button variant='flat' onPress={handleCropCancel}>
              Cancel
            </Button>
            <Button
              color='primary'
              onPress={handleCropConfirm}
              isLoading={uploading}
            >
              {uploading ? 'Uploading...' : 'Confirm'}
            </Button>
          </ModalFooter>
        </ModalContent>
      </Modal>
    </>
  );
}
