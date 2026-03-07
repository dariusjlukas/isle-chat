import { useState, useEffect } from 'react';
import {
  Button,
  Input,
  Progress,
  Select,
  SelectItem,
  Switch,
  CheckboxGroup,
  Checkbox,
  RadioGroup,
  Radio,
  Divider,
} from '@heroui/react';
import { useChatStore } from '../../stores/chatStore';
import * as api from '../../services/api';

const UNITS = [
  { key: 'MB', label: 'MB', bytes: 1024 * 1024 },
  { key: 'GB', label: 'GB', bytes: 1024 * 1024 * 1024 },
];

function toHumanUnit(bytes: number): { value: number; unit: string } {
  if (bytes >= 1024 * 1024 * 1024 && bytes % (1024 * 1024 * 1024) === 0) {
    return { value: bytes / (1024 * 1024 * 1024), unit: 'GB' };
  }
  return { value: bytes / (1024 * 1024), unit: 'MB' };
}

function formatSize(bytes: number): string {
  if (bytes === 0) return '0 B';
  if (bytes < 1024) return bytes + ' B';
  if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + ' KB';
  if (bytes < 1024 * 1024 * 1024)
    return (bytes / (1024 * 1024)).toFixed(1) + ' MB';
  return (bytes / (1024 * 1024 * 1024)).toFixed(2) + ' GB';
}

interface Props {
  isSetup?: boolean;
  onComplete?: () => void;
}

export function ServerSettings({ isSetup, onComplete }: Props) {
  const [loading, setLoading] = useState(true);
  const [saving, setSaving] = useState(false);
  const [storageUsed, setStorageUsed] = useState(0);
  const [serverArchived, setServerArchivedLocal] = useState(false);
  const setServerArchivedGlobal = useChatStore((s) => s.setServerArchived);
  const currentUser = useChatStore((s) => s.user);
  const isOwner = currentUser?.role === 'owner';

  const setServerArchived = (archived: boolean) => {
    setServerArchivedLocal(archived);
    setServerArchivedGlobal(archived);
  };

  // File settings
  const [maxFileValue, setMaxFileValue] = useState('1024');
  const [maxFileUnit, setMaxFileUnit] = useState('MB');
  const [maxStorageValue, setMaxStorageValue] = useState('0');
  const [maxStorageUnit, setMaxStorageUnit] = useState('GB');
  const [maxStorageBytes, setMaxStorageBytes] = useState(0);

  // New settings
  const [serverName, setServerName] = useState('Isle Chat');
  const [authMethods, setAuthMethods] = useState<string[]>(['passkey', 'pki']);
  const [registrationMode, setRegistrationMode] = useState('invite');
  const [fileUploadsEnabled, setFileUploadsEnabled] = useState(true);
  const [sessionExpiryHours, setSessionExpiryHours] = useState('168');

  const applySettings = (data: api.AdminSettings) => {
    setStorageUsed(data.storage_used);

    const file = toHumanUnit(data.max_file_size);
    setMaxFileValue(String(file.value));
    setMaxFileUnit(file.unit);

    setMaxStorageBytes(data.max_storage_size);
    if (data.max_storage_size > 0) {
      const storage = toHumanUnit(data.max_storage_size);
      setMaxStorageValue(String(storage.value));
      setMaxStorageUnit(storage.unit);
    } else {
      setMaxStorageValue('0');
      setMaxStorageUnit('GB');
    }

    setServerName(data.server_name);
    setAuthMethods(data.auth_methods);
    setRegistrationMode(data.registration_mode);
    setFileUploadsEnabled(data.file_uploads_enabled);
    setSessionExpiryHours(String(data.session_expiry_hours));
    setServerArchived(data.server_archived);
  };

  useEffect(() => {
    api
      .getAdminSettings()
      .then(applySettings)
      .catch(() => {})
      .finally(() => setLoading(false));
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  const buildSettingsPayload = () => {
    const fileBytes =
      parseFloat(maxFileValue) *
      (UNITS.find((u) => u.key === maxFileUnit)?.bytes || 1048576);
    const storageBytes =
      parseFloat(maxStorageValue) === 0
        ? 0
        : parseFloat(maxStorageValue) *
          (UNITS.find((u) => u.key === maxStorageUnit)?.bytes || 1073741824);

    return {
      server_name: serverName,
      auth_methods: authMethods,
      registration_mode: registrationMode,
      file_uploads_enabled: fileUploadsEnabled,
      session_expiry_hours: parseInt(sessionExpiryHours) || 168,
      max_file_size: Math.round(fileBytes),
      max_storage_size: Math.round(storageBytes),
    };
  };

  const handleSave = async () => {
    setSaving(true);
    try {
      if (isSetup) {
        await api.completeSetup(buildSettingsPayload());
        onComplete?.();
      } else {
        await api.updateAdminSettings(buildSettingsPayload());
        const data = await api.getAdminSettings();
        applySettings(data);
      }
    } catch (e) {
      console.error('Settings operation failed:', e);
    }
    setSaving(false);
  };

  if (loading)
    return <div className='text-default-500 text-sm'>Loading settings...</div>;

  const storagePercent =
    maxStorageBytes > 0
      ? Math.min((storageUsed / maxStorageBytes) * 100, 100)
      : 0;

  return (
    <div className='space-y-6'>
      {/* Server Name */}
      <div>
        <p className='text-sm font-medium text-foreground mb-2'>Server Name</p>
        <Input
          value={serverName}
          onValueChange={setServerName}
          variant='bordered'
          size='sm'
          placeholder='Isle Chat'
        />
      </div>

      <Divider />

      {/* Authentication Methods */}
      <div>
        <p className='text-sm font-medium text-foreground mb-2'>
          Authentication Methods
        </p>
        <p className='text-xs text-default-400 mb-2'>
          At least one method must be enabled. Disabling a method prevents
          existing users of that method from logging in.
        </p>
        <CheckboxGroup
          value={authMethods}
          onValueChange={(v) => {
            if (v.length > 0) setAuthMethods(v);
          }}
        >
          <Checkbox value='passkey'>Passkeys (WebAuthn)</Checkbox>
          <Checkbox value='pki'>Browser Keys (PKI)</Checkbox>
        </CheckboxGroup>
      </div>

      <Divider />

      {/* Registration Mode */}
      <div>
        <p className='text-sm font-medium text-foreground mb-2'>
          Registration Mode
        </p>
        <RadioGroup
          value={registrationMode}
          onValueChange={setRegistrationMode}
        >
          <Radio value='invite'>
            Invite Tokens
            <span className='text-xs text-default-400 ml-1'>
              - Users may use an invite token to join
            </span>
          </Radio>
          <Radio value='approval'>
            Require Approval
            <span className='text-xs text-default-400 ml-1'>
              - Admin must approve new users
            </span>
          </Radio>
          <Radio value='open'>
            Open Registration
            <span className='text-xs text-default-400 ml-1'>
              - Anyone can register
            </span>
          </Radio>
        </RadioGroup>
      </div>

      <Divider />

      {/* Session Expiry */}
      <div>
        <p className='text-sm font-medium text-foreground mb-2'>
          Session Duration (hours)
        </p>
        <Input
          type='number'
          value={sessionExpiryHours}
          onValueChange={setSessionExpiryHours}
          variant='bordered'
          size='sm'
          className='w-32'
          min='1'
        />
      </div>

      <Divider />

      {/* File Uploads */}
      <div>
        <div className='flex items-center justify-between mb-2'>
          <p className='text-sm font-medium text-foreground'>File Uploads</p>
          <Switch
            isSelected={fileUploadsEnabled}
            onValueChange={setFileUploadsEnabled}
            size='sm'
          />
        </div>

        {fileUploadsEnabled && (
          <div className='space-y-4 pl-1'>
            {!isSetup && (
              <div>
                <p className='text-sm font-medium text-foreground mb-2'>
                  Storage Used
                </p>
                <Progress
                  size='md'
                  value={
                    maxStorageBytes > 0
                      ? storagePercent
                      : storageUsed > 0
                        ? 100
                        : 0
                  }
                  color={
                    maxStorageBytes > 0
                      ? storagePercent > 90
                        ? 'danger'
                        : storagePercent > 70
                          ? 'warning'
                          : 'primary'
                      : 'primary'
                  }
                  label={
                    maxStorageBytes > 0
                      ? `${formatSize(storageUsed)} / ${formatSize(maxStorageBytes)}`
                      : `${formatSize(storageUsed)} used (no limit)`
                  }
                  showValueLabel={maxStorageBytes > 0}
                  classNames={{ label: 'text-sm', value: 'text-sm' }}
                />
              </div>
            )}

            <div>
              <p className='text-sm font-medium text-foreground mb-2'>
                Max File Upload Size
              </p>
              <div className='flex gap-2'>
                <Input
                  type='number'
                  value={maxFileValue}
                  onValueChange={setMaxFileValue}
                  variant='bordered'
                  size='sm'
                  className='w-28'
                  min='1'
                />
                <Select
                  selectedKeys={new Set([maxFileUnit])}
                  onSelectionChange={(keys) => {
                    const val = Array.from(keys)[0] as string;
                    if (val) setMaxFileUnit(val);
                  }}
                  variant='bordered'
                  size='sm'
                  className='w-24'
                >
                  {UNITS.map((u) => (
                    <SelectItem key={u.key}>{u.label}</SelectItem>
                  ))}
                </Select>
              </div>
            </div>

            <div>
              <p className='text-sm font-medium text-foreground mb-2'>
                Max Total Storage (0 = unlimited)
              </p>
              <div className='flex gap-2'>
                <Input
                  type='number'
                  value={maxStorageValue}
                  onValueChange={setMaxStorageValue}
                  variant='bordered'
                  size='sm'
                  className='w-28'
                  min='0'
                />
                <Select
                  selectedKeys={new Set([maxStorageUnit])}
                  onSelectionChange={(keys) => {
                    const val = Array.from(keys)[0] as string;
                    if (val) setMaxStorageUnit(val);
                  }}
                  variant='bordered'
                  size='sm'
                  className='w-24'
                >
                  {UNITS.map((u) => (
                    <SelectItem key={u.key}>{u.label}</SelectItem>
                  ))}
                </Select>
              </div>
            </div>
          </div>
        )}
      </div>

      <Button
        color='primary'
        size='sm'
        isLoading={saving}
        onPress={handleSave}
        fullWidth={isSetup}
      >
        {isSetup ? 'Complete Setup' : 'Save Settings'}
      </Button>

      {!isSetup && isOwner && (
        <>
          <Divider />
          <div>
            <p className='text-sm font-medium text-foreground mb-1'>
              {serverArchived ? 'Server Archived' : 'Archive Server'}
            </p>
            <p className='text-xs text-default-400 mb-3'>
              {serverArchived
                ? 'The server is currently archived. Users cannot send messages or create channels.'
                : 'Archiving the server will prevent all users from sending messages or creating channels.'}
            </p>
            {serverArchived ? (
              <Button
                color='success'
                variant='flat'
                size='sm'
                onPress={async () => {
                  try {
                    await api.unarchiveServer();
                    setServerArchived(false);
                  } catch (e) {
                    console.error('Unarchive failed:', e);
                  }
                }}
              >
                Unarchive Server
              </Button>
            ) : (
              <Button
                color='danger'
                variant='flat'
                size='sm'
                onPress={async () => {
                  if (
                    !confirm(
                      'Are you sure you want to archive the server? All messaging will be disabled.',
                    )
                  )
                    return;
                  try {
                    await api.archiveServer();
                    setServerArchived(true);
                  } catch (e) {
                    console.error('Archive failed:', e);
                  }
                }}
              >
                Archive Server
              </Button>
            )}
          </div>
        </>
      )}
    </div>
  );
}
