import { useState, useEffect } from 'react';
import { Button, Input, Progress, Select, SelectItem } from '@heroui/react';
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

export function ServerSettings() {
  const [loading, setLoading] = useState(true);
  const [saving, setSaving] = useState(false);
  const [storageUsed, setStorageUsed] = useState(0);

  const [maxFileValue, setMaxFileValue] = useState('1024');
  const [maxFileUnit, setMaxFileUnit] = useState('MB');
  const [maxStorageValue, setMaxStorageValue] = useState('0');
  const [maxStorageUnit, setMaxStorageUnit] = useState('GB');
  const [maxStorageBytes, setMaxStorageBytes] = useState(0);

  useEffect(() => {
    api
      .getAdminSettings()
      .then((data) => {
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
      })
      .catch(() => {})
      .finally(() => setLoading(false));
  }, []);

  const loadSettings = async () => {
    try {
      const data = await api.getAdminSettings();
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
    } catch {
      /* ignored */
    }
    setLoading(false);
  };

  const handleSave = async () => {
    setSaving(true);
    try {
      const fileBytes =
        parseFloat(maxFileValue) *
        (UNITS.find((u) => u.key === maxFileUnit)?.bytes || 1048576);
      const storageBytes =
        parseFloat(maxStorageValue) === 0
          ? 0
          : parseFloat(maxStorageValue) *
            (UNITS.find((u) => u.key === maxStorageUnit)?.bytes || 1073741824);

      await api.updateAdminSettings({
        max_file_size: Math.round(fileBytes),
        max_storage_size: Math.round(storageBytes),
      });
      await loadSettings();
    } catch {
      /* ignored */
    }
    setSaving(false);
  };

  if (loading)
    return <div className="text-default-500 text-sm">Loading settings...</div>;

  const storagePercent =
    maxStorageBytes > 0
      ? Math.min((storageUsed / maxStorageBytes) * 100, 100)
      : 0;

  return (
    <div>
      <div className="space-y-4">
        <div>
          <p className="text-sm font-medium text-foreground mb-2">
            Storage Used
          </p>
          <Progress
            size="md"
            value={
              maxStorageBytes > 0 ? storagePercent : storageUsed > 0 ? 100 : 0
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

        <div>
          <p className="text-sm font-medium text-foreground mb-2">
            Max File Upload Size
          </p>
          <div className="flex gap-2">
            <Input
              type="number"
              value={maxFileValue}
              onValueChange={setMaxFileValue}
              size="sm"
              className="w-28"
              min="1"
            />
            <Select
              selectedKeys={new Set([maxFileUnit])}
              onSelectionChange={(keys) => {
                const val = Array.from(keys)[0] as string;
                if (val) setMaxFileUnit(val);
              }}
              size="sm"
              className="w-24"
            >
              {UNITS.map((u) => (
                <SelectItem key={u.key}>{u.label}</SelectItem>
              ))}
            </Select>
          </div>
        </div>

        <div>
          <p className="text-sm font-medium text-foreground mb-2">
            Max Total Storage (0 = unlimited)
          </p>
          <div className="flex gap-2">
            <Input
              type="number"
              value={maxStorageValue}
              onValueChange={setMaxStorageValue}
              size="sm"
              className="w-28"
              min="0"
            />
            <Select
              selectedKeys={new Set([maxStorageUnit])}
              onSelectionChange={(keys) => {
                const val = Array.from(keys)[0] as string;
                if (val) setMaxStorageUnit(val);
              }}
              size="sm"
              className="w-24"
            >
              {UNITS.map((u) => (
                <SelectItem key={u.key}>{u.label}</SelectItem>
              ))}
            </Select>
          </div>
        </div>

        <Button
          color="primary"
          size="sm"
          isLoading={saving}
          onPress={handleSave}
        >
          Save Settings
        </Button>
      </div>
    </div>
  );
}
