import {
  faUser,
  faPalette,
  faFingerprint,
  faKey,
  faLock,
  faShieldHalved,
  faTriangleExclamation,
} from '@fortawesome/free-solid-svg-icons';
import {
  SettingsLayout,
  type SettingsCategory,
} from '../common/SettingsLayout';
import { ProfileSettings } from './ProfileSettings';
import { AppearanceSettings } from './AppearanceSettings';
import { PasskeyManager } from './PasskeyManager';
import { PkiKeyManager } from './PkiKeyManager';
import { PasswordSettings } from './PasswordSettings';
import { TotpSettings } from './TotpSettings';
import { DangerZone } from './DangerZone';

interface Props {
  onClose: () => void;
}

const categories: SettingsCategory[] = [
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
  {
    key: 'danger',
    label: 'Danger Zone',
    icon: faTriangleExclamation,
    className: 'text-danger',
    content: <DangerZone />,
  },
];

export function UserSettings({ onClose }: Props) {
  return (
    <SettingsLayout
      isOpen
      onClose={onClose}
      title='Settings'
      categories={categories}
    />
  );
}
