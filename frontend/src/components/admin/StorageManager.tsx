import { useState, useEffect } from 'react';
import { Spinner } from '@heroui/react';
import * as api from '../../services/api';
import type { SpaceStorageInfo } from '../../services/api';

function formatSize(bytes: number): string {
  if (bytes === 0) return '0 B';
  if (bytes < 1024) return bytes + ' B';
  if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + ' KB';
  if (bytes < 1024 * 1024 * 1024)
    return (bytes / (1024 * 1024)).toFixed(1) + ' MB';
  return (bytes / (1024 * 1024 * 1024)).toFixed(2) + ' GB';
}

export function StorageManager() {
  const [spaces, setSpaces] = useState<SpaceStorageInfo[]>([]);
  const [totalUsed, setTotalUsed] = useState(0);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    api
      .getAdminStorage()
      .then((data) => {
        setSpaces(data.spaces);
        setTotalUsed(data.total_used);
      })
      .catch((e) => setError(e instanceof Error ? e.message : 'Failed to load'))
      .finally(() => setLoading(false));
  }, []);

  if (loading) {
    return (
      <div className='flex justify-center py-12'>
        <Spinner size='lg' />
      </div>
    );
  }

  if (error) {
    return <div className='text-danger text-sm py-4'>{error}</div>;
  }

  const maxUsed = Math.max(...spaces.map((s) => s.storage_used), 1);

  return (
    <div className='space-y-6'>
      {/* Total overview */}
      <div className='bg-content2/50 rounded-lg p-4'>
        <p className='text-xs text-default-400 uppercase tracking-wider mb-1'>
          Total Storage Used
        </p>
        <p className='text-2xl font-semibold text-foreground'>
          {formatSize(totalUsed)}
        </p>
        <p className='text-xs text-default-400 mt-1'>
          Across {spaces.length} space{spaces.length !== 1 ? 's' : ''} with
          files
        </p>
      </div>

      {/* Per-space breakdown */}
      {spaces.length > 0 ? (
        <div className='space-y-3'>
          <h3 className='text-sm font-semibold text-foreground'>
            Per-Space Breakdown
          </h3>
          {spaces.map((s) => {
            const pct = (s.storage_used / maxUsed) * 100;
            const limitPct =
              s.storage_limit > 0
                ? Math.min((s.storage_used / s.storage_limit) * 100, 100)
                : 0;
            const nearLimit = s.storage_limit > 0 && limitPct >= 80;

            return (
              <div key={s.space_id} className='bg-content2/30 rounded-lg p-3'>
                <div className='flex items-center justify-between mb-2'>
                  <div className='min-w-0'>
                    <span className='text-sm font-medium truncate'>
                      {s.space_name}
                    </span>
                    <span className='text-xs text-default-400 ml-2'>
                      {s.file_count} file{s.file_count !== 1 ? 's' : ''}
                    </span>
                  </div>
                  <div className='text-right shrink-0'>
                    <span className='text-sm font-medium'>
                      {formatSize(s.storage_used)}
                    </span>
                    {s.storage_limit > 0 && (
                      <span className='text-xs text-default-400 ml-1'>
                        / {formatSize(s.storage_limit)}
                      </span>
                    )}
                  </div>
                </div>
                {/* Bar relative to largest space */}
                <div className='h-2 bg-default-200 rounded-full overflow-hidden'>
                  <div
                    className={`h-full rounded-full transition-all ${
                      nearLimit ? 'bg-warning' : 'bg-primary'
                    }`}
                    style={{ width: `${Math.max(pct, 1)}%` }}
                  />
                </div>
                {s.storage_limit > 0 && (
                  <p
                    className={`text-[10px] mt-1 ${nearLimit ? 'text-warning' : 'text-default-400'}`}
                  >
                    {limitPct.toFixed(0)}% of limit used
                    {nearLimit && ' — approaching limit'}
                  </p>
                )}
              </div>
            );
          })}
        </div>
      ) : (
        <p className='text-sm text-default-400 text-center py-8'>
          No spaces are using file storage yet.
        </p>
      )}
    </div>
  );
}
