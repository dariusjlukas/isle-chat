import { useState, useEffect, useMemo } from 'react';
import { Spinner, Tooltip } from '@heroui/react';
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

// Visually distinct colors for up to ~16 spaces; wraps around for more
const SPACE_COLORS = [
  '#3b82f6', // blue
  '#10b981', // emerald
  '#f59e0b', // amber
  '#ef4444', // red
  '#8b5cf6', // violet
  '#ec4899', // pink
  '#06b6d4', // cyan
  '#f97316', // orange
  '#14b8a6', // teal
  '#6366f1', // indigo
  '#84cc16', // lime
  '#e11d48', // rose
  '#0ea5e9', // sky
  '#a855f7', // purple
  '#d946ef', // fuchsia
  '#eab308', // yellow
];

export function StorageManager() {
  const [spaces, setSpaces] = useState<SpaceStorageInfo[]>([]);
  const [totalUsed, setTotalUsed] = useState(0);
  const [maxStorageBytes, setMaxStorageBytes] = useState(0);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    Promise.all([api.getAdminStorage(), api.getAdminSettings()])
      .then(([storageData, settings]) => {
        setSpaces(storageData.spaces);
        setTotalUsed(storageData.total_used);
        setMaxStorageBytes(settings.max_storage_size);
      })
      .catch((e) => setError(e instanceof Error ? e.message : 'Failed to load'))
      .finally(() => setLoading(false));
  }, []);

  // Assign a stable color to each space (sorted by name for consistency)
  const spaceColors = useMemo(() => {
    const map = new Map<string, string>();
    spaces.forEach((s, i) => {
      map.set(s.space_id, SPACE_COLORS[i % SPACE_COLORS.length]);
    });
    return map;
  }, [spaces]);

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

  // For the stacked bar: use the server limit as the denominator,
  // or if unlimited, use total_used so the bar shows relative proportions
  const barTotal = maxStorageBytes > 0 ? maxStorageBytes : totalUsed;
  const overallPercent =
    maxStorageBytes > 0
      ? Math.min((totalUsed / maxStorageBytes) * 100, 100)
      : 0;

  // Non-space storage (e.g. message attachments)
  const spaceStorageSum = spaces.reduce((sum, s) => sum + s.storage_used, 0);
  const otherStorage = Math.max(totalUsed - spaceStorageSum, 0);

  return (
    <div className='space-y-6'>
      {/* Total overview */}
      <div className='bg-content2/50 rounded-lg p-4'>
        <p className='text-xs text-default-400 uppercase tracking-wider mb-1'>
          Total Storage Used
        </p>
        <p className='text-2xl font-semibold text-foreground'>
          {formatSize(totalUsed)}
          {maxStorageBytes > 0 && (
            <span className='text-base font-normal text-default-400 ml-2'>
              / {formatSize(maxStorageBytes)}
            </span>
          )}
        </p>
        {maxStorageBytes > 0 && (
          <p className='text-xs text-default-400 mt-0.5'>
            {overallPercent.toFixed(1)}% of server limit used
          </p>
        )}
        <p className='text-xs text-default-400 mt-1'>
          Across {spaces.length} space{spaces.length !== 1 ? 's' : ''} with
          files
        </p>

        {/* Stacked bar chart */}
        {barTotal > 0 && spaces.length > 0 && (
          <div className='mt-3'>
            <div className='h-5 bg-default-200 rounded-full overflow-hidden flex'>
              {spaces
                .filter((s) => s.storage_used > 0)
                .map((s) => {
                  const widthPct = (s.storage_used / barTotal) * 100;
                  if (widthPct < 0.3) return null;
                  return (
                    <Tooltip
                      key={s.space_id}
                      content={`${s.space_name}: ${formatSize(s.storage_used)}`}
                    >
                      <div
                        className='h-full transition-all cursor-default first:rounded-l-full'
                        style={{
                          width: `${widthPct}%`,
                          backgroundColor: spaceColors.get(s.space_id),
                          minWidth: widthPct > 0 ? 3 : 0,
                        }}
                      />
                    </Tooltip>
                  );
                })}
              {otherStorage > 0 && (otherStorage / barTotal) * 100 >= 0.3 && (
                <Tooltip
                  content={`Other (messages): ${formatSize(otherStorage)}`}
                >
                  <div
                    className='h-full transition-all cursor-default'
                    style={{
                      width: `${(otherStorage / barTotal) * 100}%`,
                      backgroundColor: '#71717a',
                      minWidth: 3,
                    }}
                  />
                </Tooltip>
              )}
            </div>

            {/* Legend */}
            <div className='flex flex-wrap gap-x-3 gap-y-1 mt-2'>
              {spaces
                .filter((s) => s.storage_used > 0)
                .map((s) => (
                  <div
                    key={s.space_id}
                    className='flex items-center gap-1.5 text-xs text-default-500'
                  >
                    <span
                      className='inline-block w-2.5 h-2.5 rounded-sm shrink-0'
                      style={{
                        backgroundColor: spaceColors.get(s.space_id),
                      }}
                    />
                    <span className='truncate max-w-[120px]'>
                      {s.space_name}
                    </span>
                  </div>
                ))}
              {otherStorage > 0 && (
                <div className='flex items-center gap-1.5 text-xs text-default-500'>
                  <span
                    className='inline-block w-2.5 h-2.5 rounded-sm shrink-0'
                    style={{ backgroundColor: '#71717a' }}
                  />
                  <span>Other</span>
                </div>
              )}
            </div>
          </div>
        )}
      </div>

      {/* Per-space breakdown */}
      {spaces.length > 0 ? (
        <div className='space-y-3'>
          <h3 className='text-sm font-semibold text-foreground'>
            Per-Space Breakdown
          </h3>
          {spaces.map((s) => {
            const color = spaceColors.get(s.space_id)!;
            const barPct = barTotal > 0 ? (s.storage_used / barTotal) * 100 : 0;
            const limitPct =
              s.storage_limit > 0
                ? Math.min((s.storage_used / s.storage_limit) * 100, 100)
                : 0;
            const nearLimit = s.storage_limit > 0 && limitPct >= 80;

            return (
              <div key={s.space_id} className='bg-content2/30 rounded-lg p-3'>
                <div className='flex items-center justify-between mb-2'>
                  <div className='flex items-center gap-2 min-w-0'>
                    <span
                      className='inline-block w-3 h-3 rounded-sm shrink-0'
                      style={{ backgroundColor: color }}
                    />
                    <span className='text-sm font-medium truncate'>
                      {s.space_name}
                    </span>
                    <span className='text-xs text-default-400'>
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
                {/* Bar showing space's usage */}
                <div className='h-2 bg-default-200 rounded-full overflow-hidden'>
                  <div
                    className={`h-full rounded-full transition-all ${nearLimit ? 'bg-warning' : ''}`}
                    style={{
                      width: `${Math.max(barPct, 1)}%`,
                      ...(nearLimit ? {} : { backgroundColor: color }),
                    }}
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
