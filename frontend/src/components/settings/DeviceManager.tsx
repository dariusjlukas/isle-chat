import { useState, useEffect } from 'react';
import { Button, Card, CardBody, Alert, Spinner } from '@heroui/react';
import { QRCodeSVG } from 'qrcode.react';
import * as api from '../../services/api';
import type { DeviceKey } from '../../types';

export function DeviceManager() {
  const [devices, setDevices] = useState<DeviceKey[]>([]);
  const [deviceToken, setDeviceToken] = useState<string | null>(null);
  const [publicUrl, setPublicUrl] = useState('');
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState('');

  useEffect(() => {
    loadDevices();
    api
      .getPublicConfig()
      .then((c) => {
        setPublicUrl(c.public_url || window.location.origin);
      })
      .catch(() => {
        setPublicUrl(window.location.origin);
      });
  }, []);

  const loadDevices = async () => {
    try {
      const data = await api.listDevices();
      setDevices(data);
    } catch (e) {
      setError(e instanceof Error ? e.message : 'Failed');
    } finally {
      setLoading(false);
    }
  };

  const handleGenerateToken = async () => {
    setError('');
    try {
      const { token } = await api.createDeviceToken();
      setDeviceToken(token);
    } catch (e) {
      setError(e instanceof Error ? e.message : 'Failed');
    }
  };

  const handleRemoveDevice = async (id: string) => {
    if (!confirm('Remove this device? It will no longer be able to sign in.'))
      return;
    setError('');
    try {
      await api.removeDevice(id);
      setDevices(devices.filter((d) => d.id !== id));
    } catch (e) {
      setError(e instanceof Error ? e.message : 'Failed');
    }
  };

  const handleCopyToken = () => {
    if (deviceToken) {
      navigator.clipboard.writeText(deviceToken);
    }
  };

  if (loading)
    return (
      <div className="flex justify-center py-4">
        <Spinner size="sm" />
      </div>
    );

  return (
    <div className="space-y-4">
      <h3 className="text-lg font-semibold text-foreground">Linked Devices</h3>

      {error && (
        <Alert color="danger" variant="flat">
          {error}
        </Alert>
      )}

      <div className="space-y-2">
        {devices.map((device) => (
          <Card key={device.id}>
            <CardBody className="flex-row items-center justify-between py-2">
              <div>
                <div className="text-sm text-foreground">
                  {device.device_name}
                </div>
                <div className="text-xs text-default-500">
                  Added {new Date(device.created_at).toLocaleDateString()}
                </div>
              </div>
              {devices.length > 1 && (
                <Button
                  color="danger"
                  variant="light"
                  size="sm"
                  onPress={() => handleRemoveDevice(device.id)}
                >
                  Remove
                </Button>
              )}
            </CardBody>
          </Card>
        ))}
      </div>

      {deviceToken ? (
        <Card className="border-primary/30 bg-primary/10">
          <CardBody className="space-y-3">
            <p className="text-sm text-primary-300">
              Scan this QR code with your new device, or enter the code manually
              (expires in 15 minutes):
            </p>
            <div className="flex justify-center">
              <div className="bg-white p-3 rounded-lg">
                <QRCodeSVG
                  value={`${publicUrl}?device_token=${deviceToken}`}
                  size={180}
                  level="M"
                />
              </div>
            </div>
            <div className="flex gap-2">
              <code className="flex-1 bg-background text-primary-200 px-3 py-2 rounded font-mono text-sm break-all">
                {deviceToken}
              </code>
              <Button color="primary" size="sm" onPress={handleCopyToken}>
                Copy
              </Button>
            </div>
            <p className="text-xs text-default-500">
              On the new device, go to the login page and click "Link existing
              account to this device".
            </p>
          </CardBody>
        </Card>
      ) : (
        <Button color="primary" fullWidth onPress={handleGenerateToken}>
          Link New Device
        </Button>
      )}
    </div>
  );
}
