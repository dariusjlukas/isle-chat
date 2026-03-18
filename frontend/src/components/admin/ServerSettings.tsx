import { useState, useEffect, useRef, useMemo, useCallback } from 'react';
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
  Modal,
  ModalContent,
  ModalHeader,
  ModalBody,
  ModalFooter,
  Slider,
  Textarea,
} from '@heroui/react';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import { faCamera, faTrashCan } from '@fortawesome/free-solid-svg-icons';
import Cropper from 'react-easy-crop';
import type { Area } from 'react-easy-crop';
import { useChatStore } from '../../stores/chatStore';
import * as api from '../../services/api';
import logoLight from '../../assets/enclavestation-light-mode-icon.png';
import logoDark from '../../assets/enclavestation-dark-mode-icon.png';

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
  isSetup?: boolean;
  onComplete?: () => void;
  onDirtyChange?: (dirty: boolean) => void;
}

export function ServerSettings({ isSetup, onComplete, onDirtyChange }: Props) {
  const [loading, setLoading] = useState(true);
  const [saving, setSaving] = useState(false);
  const [storageUsed, setStorageUsed] = useState(0);
  const setServerArchivedGlobal = useChatStore((s) => s.setServerArchived);
  const setServerLockedDownGlobal = useChatStore((s) => s.setServerLockedDown);
  const setServerNameGlobal = useChatStore((s) => s.setServerName);
  const setServerIconFileIdGlobal = useChatStore((s) => s.setServerIconFileId);
  const setServerIconDarkFileIdGlobal = useChatStore(
    (s) => s.setServerIconDarkFileId,
  );

  // File settings
  const [maxFileValue, setMaxFileValue] = useState('0');
  const [maxFileUnit, setMaxFileUnit] = useState('MB');
  const [maxStorageValue, setMaxStorageValue] = useState('0');
  const [maxStorageUnit, setMaxStorageUnit] = useState('GB');

  const maxFileBytes = useMemo(() => {
    const val = parseFloat(maxFileValue);
    if (val === 0 || isNaN(val)) return 0;
    return val * (UNITS.find((u) => u.key === maxFileUnit)?.bytes || 1048576);
  }, [maxFileValue, maxFileUnit]);

  const maxStorageBytes = useMemo(() => {
    const val = parseFloat(maxStorageValue);
    if (val === 0 || isNaN(val)) return 0;
    return (
      val * (UNITS.find((u) => u.key === maxStorageUnit)?.bytes || 1073741824)
    );
  }, [maxStorageValue, maxStorageUnit]);

  // New settings
  const [serverName, setServerName] = useState('EnclaveStation');
  const [serverIconFileId, setServerIconFileId] = useState('');
  const [serverIconDarkFileId, setServerIconDarkFileId] = useState('');
  const [authMethods, setAuthMethods] = useState<string[]>(['passkey', 'pki']);
  const [registrationMode, setRegistrationMode] = useState('invite');
  const [fileUploadsEnabled, setFileUploadsEnabled] = useState(true);
  const [sessionExpiryHours, setSessionExpiryHours] = useState('168');

  // Space storage limit
  const [spaceStorageLimitValue, setSpaceStorageLimitValue] = useState('0');
  const [spaceStorageLimitUnit, setSpaceStorageLimitUnit] = useState('GB');

  const spaceStorageLimitBytes = useMemo(() => {
    const val = parseFloat(spaceStorageLimitValue);
    if (val === 0 || isNaN(val)) return 0;
    return (
      val *
      (UNITS.find((u) => u.key === spaceStorageLimitUnit)?.bytes || 1073741824)
    );
  }, [spaceStorageLimitValue, spaceStorageLimitUnit]);

  // MFA requirements
  const [mfaRequiredPassword, setMfaRequiredPassword] = useState(false);
  const [mfaRequiredPki, setMfaRequiredPki] = useState(false);
  const [mfaRequiredPasskey, setMfaRequiredPasskey] = useState(false);

  // Personal Spaces
  const [personalSpacesEnabled, setPersonalSpacesEnabled] = useState(false);
  const [personalSpacesFilesEnabled, setPersonalSpacesFilesEnabled] =
    useState(true);
  const [personalSpacesCalendarEnabled, setPersonalSpacesCalendarEnabled] =
    useState(true);
  const [personalSpacesTasksEnabled, setPersonalSpacesTasksEnabled] =
    useState(true);
  const [personalSpacesWikiEnabled, setPersonalSpacesWikiEnabled] =
    useState(true);
  const [personalSpacesMinigamesEnabled, setPersonalSpacesMinigamesEnabled] =
    useState(true);
  const [personalSpacesStorageValue, setPersonalSpacesStorageValue] =
    useState('0');
  const [personalSpacesStorageUnit, setPersonalSpacesStorageUnit] =
    useState('GB');
  const [personalSpacesTotalStorageValue, setPersonalSpacesTotalStorageValue] =
    useState('0');
  const [personalSpacesTotalStorageUnit, setPersonalSpacesTotalStorageUnit] =
    useState('GB');

  const personalSpacesTotalStorageBytes = useMemo(() => {
    const val = parseFloat(personalSpacesTotalStorageValue);
    if (val === 0 || isNaN(val)) return 0;
    return (
      val *
      (UNITS.find((u) => u.key === personalSpacesTotalStorageUnit)?.bytes ||
        1073741824)
    );
  }, [personalSpacesTotalStorageValue, personalSpacesTotalStorageUnit]);

  const personalSpacesStorageBytes = useMemo(() => {
    const val = parseFloat(personalSpacesStorageValue);
    if (val === 0 || isNaN(val)) return 0;
    return (
      val *
      (UNITS.find((u) => u.key === personalSpacesStorageUnit)?.bytes || 1048576)
    );
  }, [personalSpacesStorageValue, personalSpacesStorageUnit]);

  // LLM / AI
  const [llmEnabled, setLlmEnabled] = useState(false);
  const [llmApiUrl, setLlmApiUrl] = useState('');
  const [llmModel, setLlmModel] = useState('gpt-oss:120b');
  const [llmApiKey, setLlmApiKey] = useState('');
  const [llmMaxTokens, setLlmMaxTokens] = useState(4096);
  const [llmSystemPrompt, setLlmSystemPrompt] = useState('');

  // Password policy
  const [pwMinLength, setPwMinLength] = useState('8');
  const [pwRequireUpper, setPwRequireUpper] = useState(true);
  const [pwRequireLower, setPwRequireLower] = useState(true);
  const [pwRequireNumber, setPwRequireNumber] = useState(true);
  const [pwRequireSpecial, setPwRequireSpecial] = useState(false);
  const [pwMaxAgeDays, setPwMaxAgeDays] = useState('0');
  const [pwHistoryCount, setPwHistoryCount] = useState('0');

  // Icon upload state
  const [uploading, setUploading] = useState(false);
  const [iconMsg, setIconMsg] = useState('');
  const fileInputRef = useRef<HTMLInputElement>(null);
  const darkFileInputRef = useRef<HTMLInputElement>(null);
  const [cropImage, setCropImage] = useState<string | null>(null);
  const [cropTarget, setCropTarget] = useState<'light' | 'dark'>('light');
  const [crop, setCrop] = useState({ x: 0, y: 0 });
  const [zoom, setZoom] = useState(1);
  const [croppedArea, setCroppedArea] = useState<Area | null>(null);

  const onCropComplete = useCallback((_: Area, croppedPixels: Area) => {
    setCroppedArea(croppedPixels);
  }, []);

  const savedPayloadRef = useRef<string>('');

  const currentPayload = useMemo(
    () =>
      JSON.stringify({
        serverName,
        authMethods,
        registrationMode,
        fileUploadsEnabled,
        sessionExpiryHours,
        maxFileValue,
        maxFileUnit,
        maxStorageValue,
        maxStorageUnit,
        spaceStorageLimitValue,
        spaceStorageLimitUnit,
        pwMinLength,
        pwRequireUpper,
        pwRequireLower,
        pwRequireNumber,
        pwRequireSpecial,
        pwMaxAgeDays,
        pwHistoryCount,
        mfaRequiredPassword,
        mfaRequiredPki,
        mfaRequiredPasskey,
        personalSpacesEnabled,
        personalSpacesFilesEnabled,
        personalSpacesCalendarEnabled,
        personalSpacesTasksEnabled,
        personalSpacesWikiEnabled,
        personalSpacesMinigamesEnabled,
        personalSpacesStorageValue,
        personalSpacesStorageUnit,
        personalSpacesTotalStorageValue,
        personalSpacesTotalStorageUnit,
        llmEnabled,
        llmApiUrl,
        llmModel,
        llmApiKey,
        llmMaxTokens,
        llmSystemPrompt,
      }),
    [
      serverName,
      authMethods,
      registrationMode,
      fileUploadsEnabled,
      sessionExpiryHours,
      maxFileValue,
      maxFileUnit,
      maxStorageValue,
      maxStorageUnit,
      spaceStorageLimitValue,
      spaceStorageLimitUnit,
      pwMinLength,
      pwRequireUpper,
      pwRequireLower,
      pwRequireNumber,
      pwRequireSpecial,
      pwMaxAgeDays,
      pwHistoryCount,
      mfaRequiredPassword,
      mfaRequiredPki,
      mfaRequiredPasskey,
      personalSpacesEnabled,
      personalSpacesFilesEnabled,
      personalSpacesCalendarEnabled,
      personalSpacesTasksEnabled,
      personalSpacesWikiEnabled,
      personalSpacesMinigamesEnabled,
      personalSpacesStorageValue,
      personalSpacesStorageUnit,
      personalSpacesTotalStorageValue,
      personalSpacesTotalStorageUnit,
      llmEnabled,
      llmApiUrl,
      llmModel,
      llmApiKey,
      llmMaxTokens,
      llmSystemPrompt,
    ],
  );

  const isDirty =
    savedPayloadRef.current !== '' &&
    currentPayload !== savedPayloadRef.current;

  useEffect(() => {
    onDirtyChange?.(isDirty);
  }, [isDirty, onDirtyChange]);

  const snapshotSavedState = () => {
    savedPayloadRef.current = JSON.stringify({
      serverName,
      authMethods,
      registrationMode,
      fileUploadsEnabled,
      sessionExpiryHours,
      maxFileValue,
      maxFileUnit,
      maxStorageValue,
      maxStorageUnit,
      spaceStorageLimitValue,
      spaceStorageLimitUnit,
      pwMinLength,
      pwRequireUpper,
      pwRequireLower,
      pwRequireNumber,
      pwRequireSpecial,
      pwMaxAgeDays,
      pwHistoryCount,
      mfaRequiredPassword,
      mfaRequiredPki,
      mfaRequiredPasskey,
      personalSpacesEnabled,
      personalSpacesFilesEnabled,
      personalSpacesCalendarEnabled,
      personalSpacesTasksEnabled,
      personalSpacesWikiEnabled,
      personalSpacesMinigamesEnabled,
      personalSpacesStorageValue,
      personalSpacesStorageUnit,
      personalSpacesTotalStorageValue,
      personalSpacesTotalStorageUnit,
      llmEnabled,
      llmApiUrl,
      llmModel,
      llmApiKey,
      llmMaxTokens,
      llmSystemPrompt,
    });
  };

  const applySettings = (data: api.AdminSettings) => {
    setStorageUsed(data.storage_used);

    if (data.max_file_size > 0) {
      const file = toHumanUnit(data.max_file_size);
      setMaxFileValue(String(file.value));
      setMaxFileUnit(file.unit);
    } else {
      setMaxFileValue('0');
      setMaxFileUnit('MB');
    }

    if (data.max_storage_size > 0) {
      const storage = toHumanUnit(data.max_storage_size);
      setMaxStorageValue(String(storage.value));
      setMaxStorageUnit(storage.unit);
    } else {
      setMaxStorageValue('0');
      setMaxStorageUnit('GB');
    }

    setServerName(data.server_name);
    setServerIconFileId(data.server_icon_file_id || '');
    setServerIconDarkFileId(data.server_icon_dark_file_id || '');
    setAuthMethods(data.auth_methods);
    setRegistrationMode(data.registration_mode);
    setFileUploadsEnabled(data.file_uploads_enabled);
    setSessionExpiryHours(String(data.session_expiry_hours));
    setServerArchivedGlobal(data.server_archived);
    setServerLockedDownGlobal(data.server_locked_down);

    if (data.default_space_storage_limit > 0) {
      const ssl = toHumanUnit(data.default_space_storage_limit);
      setSpaceStorageLimitValue(String(ssl.value));
      setSpaceStorageLimitUnit(ssl.unit);
    } else {
      setSpaceStorageLimitValue('0');
      setSpaceStorageLimitUnit('GB');
    }

    setPwMinLength(String(data.password_min_length));
    setPwRequireUpper(data.password_require_uppercase);
    setPwRequireLower(data.password_require_lowercase);
    setPwRequireNumber(data.password_require_number);
    setPwRequireSpecial(data.password_require_special);
    setPwMaxAgeDays(String(data.password_max_age_days));
    setPwHistoryCount(String(data.password_history_count));

    setMfaRequiredPassword(data.mfa_required_password);
    setMfaRequiredPki(data.mfa_required_pki);
    setMfaRequiredPasskey(data.mfa_required_passkey);

    setPersonalSpacesEnabled(data.personal_spaces_enabled);
    setPersonalSpacesFilesEnabled(data.personal_spaces_files_enabled);
    setPersonalSpacesCalendarEnabled(data.personal_spaces_calendar_enabled);
    setPersonalSpacesTasksEnabled(data.personal_spaces_tasks_enabled);
    setPersonalSpacesWikiEnabled(data.personal_spaces_wiki_enabled);
    setPersonalSpacesMinigamesEnabled(data.personal_spaces_minigames_enabled);
    if (data.personal_spaces_storage_limit > 0) {
      const ps = toHumanUnit(data.personal_spaces_storage_limit);
      setPersonalSpacesStorageValue(String(ps.value));
      setPersonalSpacesStorageUnit(ps.unit);
    } else {
      setPersonalSpacesStorageValue('0');
      setPersonalSpacesStorageUnit('GB');
    }

    if (data.personal_spaces_total_storage_limit > 0) {
      const pst = toHumanUnit(data.personal_spaces_total_storage_limit);
      setPersonalSpacesTotalStorageValue(String(pst.value));
      setPersonalSpacesTotalStorageUnit(pst.unit);
    } else {
      setPersonalSpacesTotalStorageValue('0');
      setPersonalSpacesTotalStorageUnit('GB');
    }

    setLlmEnabled(data.llm_enabled ?? false);
    setLlmApiUrl(data.llm_api_url ?? '');
    setLlmModel(data.llm_model ?? 'gpt-oss:120b');
    setLlmApiKey(data.llm_api_key ?? '');
    setLlmMaxTokens(data.llm_max_tokens ?? 4096);
    setLlmSystemPrompt(data.llm_system_prompt ?? '');
  };

  useEffect(() => {
    api
      .getAdminSettings()
      .then(applySettings)
      .catch(() => {})
      .finally(() => setLoading(false));
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  // Snapshot saved state once loading completes (state has settled)
  useEffect(() => {
    if (!loading) {
      snapshotSavedState();
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [loading]);

  const buildSettingsPayload = () => {
    return {
      server_name: serverName,
      auth_methods: authMethods,
      registration_mode: registrationMode,
      file_uploads_enabled: fileUploadsEnabled,
      session_expiry_hours: parseInt(sessionExpiryHours) || 168,
      max_file_size: Math.round(maxFileBytes),
      max_storage_size: Math.round(maxStorageBytes),
      default_space_storage_limit: Math.round(spaceStorageLimitBytes),
      password_min_length: parseInt(pwMinLength) || 8,
      password_require_uppercase: pwRequireUpper,
      password_require_lowercase: pwRequireLower,
      password_require_number: pwRequireNumber,
      password_require_special: pwRequireSpecial,
      password_max_age_days: parseInt(pwMaxAgeDays) || 0,
      password_history_count: parseInt(pwHistoryCount) || 0,
      mfa_required_password: mfaRequiredPassword,
      mfa_required_pki: mfaRequiredPki,
      mfa_required_passkey: mfaRequiredPasskey,
      personal_spaces_enabled: personalSpacesEnabled,
      personal_spaces_files_enabled: personalSpacesFilesEnabled,
      personal_spaces_calendar_enabled: personalSpacesCalendarEnabled,
      personal_spaces_tasks_enabled: personalSpacesTasksEnabled,
      personal_spaces_wiki_enabled: personalSpacesWikiEnabled,
      personal_spaces_minigames_enabled: personalSpacesMinigamesEnabled,
      personal_spaces_storage_limit: Math.round(personalSpacesStorageBytes),
      personal_spaces_total_storage_limit: Math.round(
        personalSpacesTotalStorageBytes,
      ),
      llm_enabled: llmEnabled,
      llm_api_url: llmApiUrl,
      llm_model: llmModel,
      llm_api_key: llmApiKey,
      llm_max_tokens: llmMaxTokens,
      llm_system_prompt: llmSystemPrompt,
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
        // Sync to global store
        setServerNameGlobal(serverName);
        // Use setTimeout to snapshot after React state updates have flushed
        setTimeout(() => snapshotSavedState(), 0);
      }
    } catch (e) {
      console.error('Settings operation failed:', e);
    }
    setSaving(false);
  };

  // Icon upload handlers
  const handleFileSelect = (
    e: React.ChangeEvent<HTMLInputElement>,
    target: 'light' | 'dark',
  ) => {
    const file = e.target.files?.[0];
    if (!file) return;
    if (!file.type.startsWith('image/')) {
      setIconMsg('Please select an image file');
      setTimeout(() => setIconMsg(''), 3000);
      return;
    }
    const reader = new FileReader();
    reader.onload = () => {
      setCropTarget(target);
      setCropImage(reader.result as string);
      setCrop({ x: 0, y: 0 });
      setZoom(1);
    };
    reader.readAsDataURL(file);
    if (target === 'light' && fileInputRef.current)
      fileInputRef.current.value = '';
    if (target === 'dark' && darkFileInputRef.current)
      darkFileInputRef.current.value = '';
  };

  const handleCropConfirm = async () => {
    if (!cropImage || !croppedArea) return;
    setUploading(true);
    try {
      const blob = await getCroppedBlob(cropImage, croppedArea);
      const file = new File([blob], 'server-icon.png', { type: 'image/png' });
      if (cropTarget === 'dark') {
        const result = await api.uploadServerIconDark(file);
        setServerIconDarkFileId(result.server_icon_dark_file_id);
        setServerIconDarkFileIdGlobal(result.server_icon_dark_file_id);
      } else {
        const result = await api.uploadServerIcon(file);
        setServerIconFileId(result.server_icon_file_id);
        setServerIconFileIdGlobal(result.server_icon_file_id);
      }
    } catch (e) {
      setIconMsg(e instanceof Error ? e.message : 'Upload failed');
      setTimeout(() => setIconMsg(''), 3000);
    } finally {
      setUploading(false);
      setCropImage(null);
    }
  };

  const handleRemoveIcon = async (target: 'light' | 'dark') => {
    setUploading(true);
    try {
      if (target === 'dark') {
        await api.deleteServerIconDark();
        setServerIconDarkFileId('');
        setServerIconDarkFileIdGlobal(null);
      } else {
        await api.deleteServerIcon();
        setServerIconFileId('');
        setServerIconFileIdGlobal(null);
      }
    } catch (e) {
      setIconMsg(e instanceof Error ? e.message : 'Failed to remove');
      setTimeout(() => setIconMsg(''), 3000);
    } finally {
      setUploading(false);
    }
  };

  if (loading)
    return <div className='text-default-500 text-sm'>Loading settings...</div>;

  const storagePercent =
    maxStorageBytes > 0
      ? Math.min((storageUsed / maxStorageBytes) * 100, 100)
      : 0;

  const iconUrl = serverIconFileId ? api.getAvatarUrl(serverIconFileId) : null;
  const darkIconUrl = serverIconDarkFileId
    ? api.getAvatarUrl(serverIconDarkFileId)
    : null;

  const renderIconUploader = (
    target: 'light' | 'dark',
    url: string | null,
    fileId: string,
    inputRef: React.RefObject<HTMLInputElement | null>,
  ) => (
    <div className='flex flex-col items-center gap-1'>
      <p className='text-xs text-default-500 mb-1'>
        {target === 'light' ? 'Light Mode' : 'Dark Mode'}
      </p>
      <div className='relative group'>
        <img
          src={url || (target === 'light' ? logoLight : logoDark)}
          alt={`Server icon (${target})`}
          className={`w-16 h-16 rounded-xl object-cover ${target === 'dark' ? 'bg-black' : 'bg-white'}`}
        />
        <button
          type='button'
          className='absolute inset-0 rounded-xl bg-black/50 flex items-center justify-center opacity-0 group-hover:opacity-100 transition-opacity cursor-pointer'
          onClick={() => inputRef.current?.click()}
          disabled={uploading}
        >
          <FontAwesomeIcon icon={faCamera} className='text-white text-lg' />
        </button>
        <input
          ref={inputRef}
          type='file'
          accept='image/*'
          className='hidden'
          onChange={(e) => handleFileSelect(e, target)}
        />
      </div>
      <div className='flex gap-1'>
        <Button
          size='sm'
          variant='flat'
          onPress={() => inputRef.current?.click()}
          isLoading={uploading}
          className='text-xs min-w-0 h-6 px-2'
        >
          {uploading ? '...' : 'Change'}
        </Button>
        {fileId && (
          <Button
            size='sm'
            variant='light'
            color='danger'
            onPress={() => handleRemoveIcon(target)}
            isLoading={uploading}
            isIconOnly
            className='h-6 w-6 min-w-0'
          >
            <FontAwesomeIcon icon={faTrashCan} className='text-xs' />
          </Button>
        )}
      </div>
    </div>
  );

  return (
    <>
      <div className='space-y-6'>
        {/* Server Identity */}
        <div>
          <p className='text-sm font-medium text-foreground mb-3'>
            Server Identity
          </p>
          <div className='flex items-start gap-4'>
            <div className='flex flex-col items-center gap-1'>
              <div className='flex gap-3'>
                {renderIconUploader(
                  'light',
                  iconUrl,
                  serverIconFileId,
                  fileInputRef,
                )}
                {renderIconUploader(
                  'dark',
                  darkIconUrl,
                  serverIconDarkFileId,
                  darkFileInputRef,
                )}
              </div>
              {(serverIconFileId || serverIconDarkFileId) && (
                <Button
                  size='sm'
                  variant='light'
                  color='default'
                  onPress={async () => {
                    setUploading(true);
                    try {
                      if (serverIconFileId) {
                        await api.deleteServerIcon();
                        setServerIconFileId('');
                        setServerIconFileIdGlobal(null);
                      }
                      if (serverIconDarkFileId) {
                        await api.deleteServerIconDark();
                        setServerIconDarkFileId('');
                        setServerIconDarkFileIdGlobal(null);
                      }
                    } catch (e) {
                      setIconMsg(
                        e instanceof Error ? e.message : 'Failed to reset',
                      );
                      setTimeout(() => setIconMsg(''), 3000);
                    } finally {
                      setUploading(false);
                    }
                  }}
                  isLoading={uploading}
                  className='text-xs'
                >
                  Reset to default
                </Button>
              )}
              {iconMsg && (
                <span className='text-xs text-danger'>{iconMsg}</span>
              )}
            </div>
            <div className='flex-1'>
              <Input
                label='Server Name'
                value={serverName}
                onValueChange={setServerName}
                variant='bordered'
                size='sm'
                placeholder='EnclaveStation'
              />
              <p className='text-xs text-default-400 mt-1'>
                Shown in the header, browser tab, and login page
              </p>
            </div>
          </div>
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
            <Checkbox value='password'>Password</Checkbox>
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
              Invite Token or Approval
              <span className='text-xs text-default-400 ml-1'>
                - Users may join with an invite token or request access
              </span>
            </Radio>
            <Radio value='invite_only'>
              Invite Token Only
              <span className='text-xs text-default-400 ml-1'>
                - Users must have an invite token to register
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

        {/* MFA Requirements */}
        <div>
          <p className='text-sm font-medium text-foreground mb-2'>
            Multi-Factor Authentication
          </p>
          <p className='text-xs text-default-400 mb-3'>
            Require users to set up an authenticator app (TOTP) for additional
            login security. Users can also enable MFA voluntarily from their
            settings.
          </p>
          <div className='space-y-2 pl-1'>
            {authMethods.includes('password') && (
              <div className='flex items-center justify-between'>
                <span className='text-sm text-foreground'>
                  Require MFA for password login
                </span>
                <Switch
                  isSelected={mfaRequiredPassword}
                  onValueChange={setMfaRequiredPassword}
                  size='sm'
                />
              </div>
            )}
            {authMethods.includes('pki') && (
              <div className='flex items-center justify-between'>
                <span className='text-sm text-foreground'>
                  Require MFA for browser key login
                </span>
                <Switch
                  isSelected={mfaRequiredPki}
                  onValueChange={setMfaRequiredPki}
                  size='sm'
                />
              </div>
            )}
            {authMethods.includes('passkey') && (
              <div className='flex items-center justify-between'>
                <div>
                  <span className='text-sm text-foreground'>
                    Require MFA for passkey login
                  </span>
                </div>
                <Switch
                  isSelected={mfaRequiredPasskey}
                  onValueChange={setMfaRequiredPasskey}
                  size='sm'
                />
              </div>
            )}
          </div>
        </div>

        {authMethods.includes('password') && (
          <>
            <Divider />

            {/* Password Policy */}
            <div>
              <p className='text-sm font-medium text-foreground mb-2'>
                Password Policy
              </p>
              <div className='space-y-3 pl-1'>
                <div>
                  <p className='text-xs text-default-500 mb-1'>
                    Minimum Length
                  </p>
                  <Input
                    type='number'
                    value={pwMinLength}
                    onValueChange={setPwMinLength}
                    variant='bordered'
                    size='sm'
                    className='w-32'
                    min='1'
                  />
                </div>
                <CheckboxGroup
                  label='Require'
                  value={
                    [
                      pwRequireUpper && 'upper',
                      pwRequireLower && 'lower',
                      pwRequireNumber && 'number',
                      pwRequireSpecial && 'special',
                    ].filter(Boolean) as string[]
                  }
                  onValueChange={(v) => {
                    setPwRequireUpper(v.includes('upper'));
                    setPwRequireLower(v.includes('lower'));
                    setPwRequireNumber(v.includes('number'));
                    setPwRequireSpecial(v.includes('special'));
                  }}
                  size='sm'
                >
                  <Checkbox value='upper'>Uppercase letter</Checkbox>
                  <Checkbox value='lower'>Lowercase letter</Checkbox>
                  <Checkbox value='number'>Number</Checkbox>
                  <Checkbox value='special'>Special character</Checkbox>
                </CheckboxGroup>
                <div>
                  <p className='text-xs text-default-500 mb-1'>
                    Maximum Password Age (days, 0 = no expiry)
                  </p>
                  <Input
                    type='number'
                    value={pwMaxAgeDays}
                    onValueChange={setPwMaxAgeDays}
                    variant='bordered'
                    size='sm'
                    className='w-32'
                    min='0'
                  />
                </div>
                <div>
                  <p className='text-xs text-default-500 mb-1'>
                    Password History (0 = no restriction)
                  </p>
                  <Input
                    type='number'
                    value={pwHistoryCount}
                    onValueChange={setPwHistoryCount}
                    variant='bordered'
                    size='sm'
                    className='w-32'
                    min='0'
                    description='Number of previous passwords that cannot be reused'
                  />
                </div>
              </div>
            </div>
          </>
        )}

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
                  Max Individual File Upload Size (0 = unlimited)
                </p>
                <div className='flex gap-2'>
                  <Input
                    type='number'
                    value={maxFileValue}
                    onValueChange={setMaxFileValue}
                    variant='bordered'
                    size='sm'
                    className='w-28'
                    min='0'
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

              <div>
                <p className='text-sm font-medium text-foreground mb-2'>
                  Storage Limit per Space (0 = unlimited)
                </p>
                <div className='flex gap-2'>
                  <Input
                    type='number'
                    value={spaceStorageLimitValue}
                    onValueChange={setSpaceStorageLimitValue}
                    variant='bordered'
                    size='sm'
                    className='w-28'
                    min='0'
                  />
                  <Select
                    selectedKeys={new Set([spaceStorageLimitUnit])}
                    onSelectionChange={(keys) => {
                      const val = Array.from(keys)[0] as string;
                      if (val) setSpaceStorageLimitUnit(val);
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

        <>
          <Divider />

          {/* Personal Spaces */}
          <div>
            <div className='flex items-center justify-between mb-2'>
              <p className='text-sm font-medium text-foreground'>
                Personal Spaces
              </p>
              <Switch
                isSelected={personalSpacesEnabled}
                onValueChange={setPersonalSpacesEnabled}
                size='sm'
              />
            </div>
            <p className='text-xs text-default-400 mb-3'>
              Allow users to create a personal space with private tools.
            </p>

            {personalSpacesEnabled && (
              <div className='space-y-3 pl-1'>
                <div className='space-y-2'>
                  <p className='text-xs text-default-500'>Enabled Tools</p>
                  <div className='flex items-center justify-between'>
                    <span className='text-sm text-foreground'>Files</span>
                    <Switch
                      isSelected={personalSpacesFilesEnabled}
                      onValueChange={setPersonalSpacesFilesEnabled}
                      size='sm'
                    />
                  </div>
                  <div className='flex items-center justify-between'>
                    <span className='text-sm text-foreground'>Calendar</span>
                    <Switch
                      isSelected={personalSpacesCalendarEnabled}
                      onValueChange={setPersonalSpacesCalendarEnabled}
                      size='sm'
                    />
                  </div>
                  <div className='flex items-center justify-between'>
                    <span className='text-sm text-foreground'>Tasks</span>
                    <Switch
                      isSelected={personalSpacesTasksEnabled}
                      onValueChange={setPersonalSpacesTasksEnabled}
                      size='sm'
                    />
                  </div>
                  <div className='flex items-center justify-between'>
                    <span className='text-sm text-foreground'>Wiki</span>
                    <Switch
                      isSelected={personalSpacesWikiEnabled}
                      onValueChange={setPersonalSpacesWikiEnabled}
                      size='sm'
                    />
                  </div>
                  <div className='flex items-center justify-between'>
                    <span className='text-sm text-foreground'>Minigames</span>
                    <Switch
                      isSelected={personalSpacesMinigamesEnabled}
                      onValueChange={setPersonalSpacesMinigamesEnabled}
                      size='sm'
                    />
                  </div>
                </div>

                <div>
                  <p className='text-sm font-medium text-foreground mb-2'>
                    Total Storage Limit for All Personal Spaces (0 = unlimited)
                  </p>
                  <div className='flex gap-2'>
                    <Input
                      type='number'
                      value={personalSpacesTotalStorageValue}
                      onValueChange={setPersonalSpacesTotalStorageValue}
                      variant='bordered'
                      size='sm'
                      className='w-28'
                      min='0'
                    />
                    <Select
                      selectedKeys={new Set([personalSpacesTotalStorageUnit])}
                      onSelectionChange={(keys) => {
                        const val = Array.from(keys)[0] as string;
                        if (val) setPersonalSpacesTotalStorageUnit(val);
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
                    Storage Limit per User (0 = unlimited)
                  </p>
                  <div className='flex gap-2'>
                    <Input
                      type='number'
                      value={personalSpacesStorageValue}
                      onValueChange={setPersonalSpacesStorageValue}
                      variant='bordered'
                      size='sm'
                      className='w-28'
                      min='0'
                    />
                    <Select
                      selectedKeys={new Set([personalSpacesStorageUnit])}
                      onSelectionChange={(keys) => {
                        const val = Array.from(keys)[0] as string;
                        if (val) setPersonalSpacesStorageUnit(val);
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
        </>

        <Divider />

        {/* AI / LLM */}
        <div>
          <div className='flex items-center justify-between mb-2'>
            <p className='text-sm font-medium text-foreground'>AI Assistant</p>
            <Switch
              isSelected={llmEnabled}
              onValueChange={setLlmEnabled}
              size='sm'
            />
          </div>

          {llmEnabled && (
            <div className='space-y-3 pl-1'>
              <div>
                <p className='text-xs text-default-500 mb-1'>LLM API URL</p>
                <Input
                  placeholder='http://localhost:8080/v1'
                  value={llmApiUrl}
                  onValueChange={setLlmApiUrl}
                  variant='bordered'
                  size='sm'
                />
              </div>
              <div>
                <p className='text-xs text-default-500 mb-1'>Model</p>
                <Input
                  placeholder='gpt-oss:120b'
                  value={llmModel}
                  onValueChange={setLlmModel}
                  variant='bordered'
                  size='sm'
                  className='w-64'
                />
              </div>
              <div>
                <p className='text-xs text-default-500 mb-1'>
                  API Key (optional)
                </p>
                <Input
                  type='password'
                  placeholder='Optional'
                  value={llmApiKey}
                  onValueChange={setLlmApiKey}
                  variant='bordered'
                  size='sm'
                  className='w-64'
                />
              </div>
              <div>
                <p className='text-xs text-default-500 mb-1'>Max Tokens</p>
                <Input
                  type='number'
                  value={String(llmMaxTokens)}
                  onValueChange={(v) => {
                    setLlmMaxTokens(Number(v) || 4096);
                  }}
                  variant='bordered'
                  size='sm'
                  className='w-32'
                  min='1'
                />
              </div>
              <div>
                <p className='text-xs text-default-500 mb-1'>
                  System Prompt (optional)
                </p>
                <Textarea
                  placeholder='Override the default AI system prompt...'
                  value={llmSystemPrompt}
                  onValueChange={setLlmSystemPrompt}
                  variant='bordered'
                  size='sm'
                  minRows={3}
                />
              </div>
            </div>
          )}
        </div>

        <Button
          color={isDirty ? 'warning' : 'primary'}
          size='sm'
          isLoading={saving}
          onPress={handleSave}
          fullWidth={isSetup}
        >
          {isSetup
            ? 'Complete Setup'
            : isDirty
              ? 'Save Settings (unsaved changes)'
              : 'Save Settings'}
        </Button>
      </div>

      {/* Crop Modal */}
      <Modal
        isOpen={!!cropImage}
        onOpenChange={(open) => !open && setCropImage(null)}
        size='lg'
        backdrop='opaque'
      >
        <ModalContent>
          <ModalHeader>Crop Server Icon</ModalHeader>
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
            <Button variant='flat' onPress={() => setCropImage(null)}>
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
