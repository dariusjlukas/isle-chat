import { useState } from 'react';
import {
  Modal,
  ModalContent,
  ModalHeader,
  ModalBody,
  Button,
  Input,
  Textarea,
  Alert,
  Select,
  SelectItem,
  Accordion,
  AccordionItem,
} from '@heroui/react';
import { useChatStore } from '../../stores/chatStore';
import * as api from '../../services/api';
import { PasskeyManager } from './PasskeyManager';
import { PkiKeyManager } from './PkiKeyManager';
import {
  useTheme,
  COLOR_THEMES,
  type ColorTheme,
  type ModeSetting,
} from '../../hooks/useTheme';

interface Props {
  onClose: () => void;
}

export function UserSettings({ onClose }: Props) {
  const user = useChatStore((s) => s.user);
  const updateUser = useChatStore((s) => s.updateUser);
  const clearAuth = useChatStore((s) => s.clearAuth);

  const [displayName, setDisplayName] = useState(user?.display_name || '');
  const [bio, setBio] = useState(user?.bio || '');
  const [status, setStatus] = useState(user?.status || '');
  const [saving, setSaving] = useState(false);
  const [saveMsg, setSaveMsg] = useState('');

  const { colorTheme, modeSetting, setColorTheme, setModeSetting } = useTheme();

  const [deleteConfirm, setDeleteConfirm] = useState('');
  const [deleting, setDeleting] = useState(false);
  const [deleteError, setDeleteError] = useState('');

  const handleSaveProfile = async (e: React.FormEvent) => {
    e.preventDefault();
    setSaving(true);
    setSaveMsg('');
    try {
      const updated = await api.updateProfile({
        display_name: displayName.trim(),
        bio: bio.trim(),
        status: status.trim(),
      });
      updateUser({
        display_name: updated.display_name,
        bio: updated.bio,
        status: updated.status,
      });
      setSaveMsg('Saved');
      setTimeout(() => setSaveMsg(''), 2000);
    } catch (e) {
      setSaveMsg(e instanceof Error ? e.message : 'Failed to save');
    } finally {
      setSaving(false);
    }
  };

  const handleDeleteAccount = async () => {
    if (deleteConfirm !== user?.username) return;
    setDeleting(true);
    setDeleteError('');
    try {
      await api.deleteAccount();
      clearAuth();
    } catch (e) {
      setDeleteError(
        e instanceof Error ? e.message : 'Failed to delete account',
      );
      setDeleting(false);
    }
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
        <ModalHeader>Settings</ModalHeader>
        <ModalBody className="pb-6">
          <Accordion
            variant="splitted"
            selectionMode="multiple"
            defaultExpandedKeys={[]}
          >
            <AccordionItem key="profile" title="Profile">
              <form onSubmit={handleSaveProfile} className="space-y-3">
                <Input
                  label="Display Name"
                  variant="bordered"
                  value={displayName}
                  onChange={(e) => setDisplayName(e.target.value)}
                />
                <Input
                  label="Status"
                  variant="bordered"
                  value={status}
                  onChange={(e) => setStatus(e.target.value)}
                  maxLength={100}
                  placeholder="What are you up to?"
                />
                <Textarea
                  label="Bio"
                  variant="bordered"
                  value={bio}
                  onChange={(e) => setBio(e.target.value)}
                  minRows={3}
                  placeholder="Tell us about yourself"
                />
                <div className="flex items-center gap-3">
                  <Button
                    type="submit"
                    color="primary"
                    isLoading={saving}
                    size="sm"
                  >
                    {saving ? 'Saving...' : 'Save'}
                  </Button>
                  {saveMsg && (
                    <span
                      className={`text-sm ${saveMsg === 'Saved' ? 'text-success' : 'text-danger'}`}
                    >
                      {saveMsg}
                    </span>
                  )}
                </div>
              </form>
            </AccordionItem>

            <AccordionItem key="appearance" title="Appearance">
              <div className="flex flex-col sm:flex-row gap-3">
                <Select
                  label="Color Theme"
                  variant="bordered"
                  selectedKeys={[colorTheme]}
                  onChange={(e) => {
                    if (e.target.value)
                      setColorTheme(e.target.value as ColorTheme);
                  }}
                  className="flex-1"
                >
                  {COLOR_THEMES.map(({ key, label }) => (
                    <SelectItem key={key}>{label}</SelectItem>
                  ))}
                </Select>
                <Select
                  label="Mode"
                  variant="bordered"
                  selectedKeys={[modeSetting]}
                  onChange={(e) => {
                    if (e.target.value)
                      setModeSetting(e.target.value as ModeSetting);
                  }}
                  className="flex-1"
                >
                  <SelectItem key="auto">Auto</SelectItem>
                  <SelectItem key="light">Light</SelectItem>
                  <SelectItem key="dark">Dark</SelectItem>
                </Select>
              </div>
            </AccordionItem>

            <AccordionItem key="passkeys" title="Passkeys">
              <PasskeyManager />
            </AccordionItem>

            <AccordionItem key="browser-keys" title="Browser Keys">
              <PkiKeyManager />
            </AccordionItem>

            <AccordionItem
              key="danger"
              title="Danger Zone"
              classNames={{ title: 'text-danger' }}
            >
              <div className="space-y-4">
                <div>
                  <p className="text-sm text-default-500 mb-2">
                    Permanently delete your account and all associated data.
                    This cannot be undone.
                  </p>
                  {deleteError && (
                    <Alert color="danger" variant="flat" className="mb-2">
                      {deleteError}
                    </Alert>
                  )}
                  <div className="flex gap-2">
                    <Input
                      size="sm"
                      variant="bordered"
                      color="danger"
                      value={deleteConfirm}
                      onChange={(e) => setDeleteConfirm(e.target.value)}
                      placeholder={`Type "${user?.username}" to confirm`}
                      className="flex-1"
                    />
                    <Button
                      color="danger"
                      size="sm"
                      isDisabled={deleteConfirm !== user?.username}
                      isLoading={deleting}
                      onPress={handleDeleteAccount}
                    >
                      {deleting ? 'Deleting...' : 'Delete Account'}
                    </Button>
                  </div>
                </div>
              </div>
            </AccordionItem>
          </Accordion>
        </ModalBody>
      </ModalContent>
    </Modal>
  );
}
