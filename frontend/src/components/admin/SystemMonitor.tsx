import { useState, useEffect, useRef } from 'react';
import { Spinner } from '@heroui/react';
import * as api from '../../services/api';
import type { SystemStats } from '../../services/api';

function formatKb(kb: number): string {
  if (kb < 0) return 'N/A';
  if (kb < 1024) return kb + ' KB';
  if (kb < 1024 * 1024) return (kb / 1024).toFixed(1) + ' MB';
  return (kb / (1024 * 1024)).toFixed(2) + ' GB';
}

function formatBytes(bytes: number): string {
  if (bytes < 1024) return Math.round(bytes) + ' B';
  if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + ' KB';
  if (bytes < 1024 * 1024 * 1024)
    return (bytes / (1024 * 1024)).toFixed(1) + ' MB';
  return (bytes / (1024 * 1024 * 1024)).toFixed(2) + ' GB';
}

function formatRate(bytesPerSec: number): string {
  return formatBytes(bytesPerSec) + '/s';
}

function GaugeRing({
  percent,
  label,
  detail,
  color,
}: {
  percent: number;
  label: string;
  detail: string;
  color: string;
}) {
  const clamped = Math.min(Math.max(percent, 0), 100);
  const radius = 40;
  const circumference = 2 * Math.PI * radius;
  const offset = circumference * (1 - clamped / 100);

  // color thresholds
  const ringColor =
    clamped >= 90 ? '#ef4444' : clamped >= 70 ? '#f59e0b' : color;

  return (
    <div className='flex flex-col items-center gap-1.5'>
      <div className='relative w-24 h-24'>
        <svg className='w-full h-full -rotate-90' viewBox='0 0 96 96'>
          {/* Background track */}
          <circle
            cx='48'
            cy='48'
            r={radius}
            fill='none'
            stroke='currentColor'
            className='text-default-200'
            strokeWidth='8'
          />
          {/* Filled arc */}
          <circle
            cx='48'
            cy='48'
            r={radius}
            fill='none'
            stroke={ringColor}
            strokeWidth='8'
            strokeLinecap='round'
            strokeDasharray={circumference}
            strokeDashoffset={offset}
            className='transition-all duration-700 ease-out'
          />
        </svg>
        <div className='absolute inset-0 flex flex-col items-center justify-center'>
          <span className='text-lg font-semibold text-foreground'>
            {clamped.toFixed(1)}%
          </span>
        </div>
      </div>
      <span className='text-xs font-medium text-foreground'>{label}</span>
      <span className='text-[10px] text-default-400 text-center leading-tight'>
        {detail}
      </span>
    </div>
  );
}

export function SystemMonitor() {
  const [stats, setStats] = useState<SystemStats | null>(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const intervalRef = useRef<ReturnType<typeof setInterval>>(null);
  const prevNetRef = useRef<{
    rx: number;
    tx: number;
    time: number;
  } | null>(null);
  const [netRate, setNetRate] = useState<{
    rx: number;
    tx: number;
  } | null>(null);

  const fetchStats = () => {
    api
      .getSystemStats()
      .then((data) => {
        // Compute network rate from delta between polls
        const now = Date.now();
        const prev = prevNetRef.current;
        if (prev) {
          const elapsed = (now - prev.time) / 1000; // seconds
          if (elapsed > 0) {
            setNetRate({
              rx: Math.max(0, (data.net_rx_bytes - prev.rx) / elapsed),
              tx: Math.max(0, (data.net_tx_bytes - prev.tx) / elapsed),
            });
          }
        }
        prevNetRef.current = {
          rx: data.net_rx_bytes,
          tx: data.net_tx_bytes,
          time: now,
        };

        setStats(data);
        setError(null);
      })
      .catch((e) => setError(e instanceof Error ? e.message : 'Failed to load'))
      .finally(() => setLoading(false));
  };

  useEffect(() => {
    fetchStats();
    // Poll every 5 seconds
    intervalRef.current = setInterval(fetchStats, 5000);
    return () => {
      if (intervalRef.current) clearInterval(intervalRef.current);
    };
  }, []);

  if (loading && !stats) {
    return (
      <div className='flex justify-center py-12'>
        <Spinner size='lg' />
      </div>
    );
  }

  if (error && !stats) {
    return <div className='text-danger text-sm py-4'>{error}</div>;
  }

  if (!stats) return null;

  const memUsedKb = stats.mem_total_kb - stats.mem_available_kb;
  const memPercent =
    stats.mem_total_kb > 0 ? (memUsedKb / stats.mem_total_kb) * 100 : 0;

  const swapUsedKb = stats.swap_total_kb - stats.swap_free_kb;
  const swapPercent =
    stats.swap_total_kb > 0 ? (swapUsedKb / stats.swap_total_kb) * 100 : 0;

  return (
    <div className='space-y-6'>
      {/* Gauges */}
      <div className='flex justify-center gap-6 flex-wrap'>
        <GaugeRing
          percent={stats.cpu_percent}
          label='CPU'
          detail={`Load: ${stats.load_1m} / ${stats.load_5m} / ${stats.load_15m}`}
          color='#3b82f6'
        />
        <GaugeRing
          percent={memPercent}
          label='Memory'
          detail={`${formatKb(memUsedKb)} / ${formatKb(stats.mem_total_kb)}`}
          color='#10b981'
        />
        {stats.swap_total_kb > 0 && (
          <GaugeRing
            percent={swapPercent}
            label='Swap'
            detail={`${formatKb(swapUsedKb)} / ${formatKb(stats.swap_total_kb)}`}
            color='#8b5cf6'
          />
        )}
      </div>

      {/* Detailed breakdown */}
      <div className='space-y-3'>
        <h3 className='text-sm font-semibold text-foreground'>Details</h3>

        <div className='bg-content2/30 rounded-lg p-3 space-y-2'>
          <div className='flex justify-between text-sm'>
            <span className='text-default-500'>CPU Usage</span>
            <span className='font-medium'>{stats.cpu_percent}%</span>
          </div>
          <div className='flex justify-between text-sm'>
            <span className='text-default-500'>
              Load Average (1 / 5 / 15 min)
            </span>
            <span className='font-medium'>
              {stats.load_1m} / {stats.load_5m} / {stats.load_15m}
            </span>
          </div>
        </div>

        <div className='bg-content2/30 rounded-lg p-3 space-y-2'>
          <div className='flex justify-between text-sm'>
            <span className='text-default-500'>Total Memory</span>
            <span className='font-medium'>{formatKb(stats.mem_total_kb)}</span>
          </div>
          <div className='flex justify-between text-sm'>
            <span className='text-default-500'>Used Memory</span>
            <span className='font-medium'>{formatKb(memUsedKb)}</span>
          </div>
          <div className='flex justify-between text-sm'>
            <span className='text-default-500'>Available Memory</span>
            <span className='font-medium'>
              {formatKb(stats.mem_available_kb)}
            </span>
          </div>
        </div>

        {stats.swap_total_kb > 0 && (
          <div className='bg-content2/30 rounded-lg p-3 space-y-2'>
            <div className='flex justify-between text-sm'>
              <span className='text-default-500'>Total Swap</span>
              <span className='font-medium'>
                {formatKb(stats.swap_total_kb)}
              </span>
            </div>
            <div className='flex justify-between text-sm'>
              <span className='text-default-500'>Used Swap</span>
              <span className='font-medium'>{formatKb(swapUsedKb)}</span>
            </div>
          </div>
        )}

        <div className='bg-content2/30 rounded-lg p-3 space-y-2'>
          <div className='flex justify-between text-sm'>
            <span className='text-default-500'>Network Received</span>
            <span className='font-medium'>
              {formatBytes(stats.net_rx_bytes)}
              {netRate && (
                <span className='text-default-400 ml-1.5 font-normal'>
                  ({formatRate(netRate.rx)})
                </span>
              )}
            </span>
          </div>
          <div className='flex justify-between text-sm'>
            <span className='text-default-500'>Network Sent</span>
            <span className='font-medium'>
              {formatBytes(stats.net_tx_bytes)}
              {netRate && (
                <span className='text-default-400 ml-1.5 font-normal'>
                  ({formatRate(netRate.tx)})
                </span>
              )}
            </span>
          </div>
        </div>
      </div>

      {error && (
        <p className='text-[10px] text-warning text-center'>
          Last poll failed — displaying stale data
        </p>
      )}
    </div>
  );
}
