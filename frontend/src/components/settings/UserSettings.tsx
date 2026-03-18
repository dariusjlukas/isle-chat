import { useMemo } from 'react';
import {
  faUser,
  faPalette,
  faFingerprint,
  faKey,
  faLock,
  faShieldHalved,
  faTriangleExclamation,
  faLaptop,
  faHexagonNodes,
} from '@fortawesome/free-solid-svg-icons';
import {
  SettingsLayout,
  type SettingsCategory,
} from '../common/SettingsLayout';
import { useChatStore } from '../../stores/chatStore';
import { ProfileSettings } from './ProfileSettings';
import { AppearanceSettings } from './AppearanceSettings';
import { PasskeyManager } from './PasskeyManager';
import { PkiKeyManager } from './PkiKeyManager';
import { PasswordSettings } from './PasswordSettings';
import { TotpSettings } from './TotpSettings';
import { DeviceManager } from './DeviceManager';
import { DangerZone } from './DangerZone';
import { AiSettings } from './AiSettings';

interface Props {
  onClose: () => void;
}

export function UserSettings({ onClose }: Props) {
  const llmEnabled = useChatStore((s) => s.llmEnabled);

  const categories: SettingsCategory[] = useMemo(
    () => [
      {
        key: 'profile',
        label: 'Profile',
        icon: faUser,
        content: <ProfileSettings />,
      },
      {
        key: 'appearance',
        label: 'Appearance',
        icon: faPalette,
        content: <AppearanceSettings />,
      },
      {
        key: 'passkeys',
        label: 'Passkeys',
        icon: faFingerprint,
        content: <PasskeyManager />,
      },
      {
        key: 'browser-keys',
        label: 'Browser Keys',
        icon: faKey,
        content: <PkiKeyManager />,
      },
      {
        key: 'devices',
        label: 'Linked Devices',
        icon: faLaptop,
        content: <DeviceManager />,
      },
      {
        key: 'password',
        label: 'Password',
        icon: faLock,
        content: <PasswordSettings />,
      },
      {
        key: 'two-factor',
        label: 'Two-Factor Auth',
        icon: faShieldHalved,
        content: <TotpSettings />,
      },
      ...(llmEnabled
        ? [
            {
              key: 'ai-assistant',
              label: 'AI Assistant',
              icon: faHexagonNodes,
              content: <AiSettings />,
            },
          ]
        : []),
      {
        key: 'danger',
        label: 'Danger Zone',
        icon: faTriangleExclamation,
        className: 'text-danger',
        content: <DangerZone />,
      },
    ],
    [llmEnabled],
  );

  return (
    <SettingsLayout
      isOpen
      onClose={onClose}
      title='Settings'
      categories={categories}
    />
  );
}
