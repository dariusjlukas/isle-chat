import { useState, useEffect, useCallback } from 'react';
import { Select, SelectItem, Input } from '@heroui/react';

interface Props {
  value: string;
  onChange: (rrule: string) => void;
}

type Freq = 'DAILY' | 'WEEKLY' | 'MONTHLY' | 'YEARLY';
type EndType = 'never' | 'count' | 'until';

const WEEKDAYS = [
  { key: 'MO', label: 'Mon' },
  { key: 'TU', label: 'Tue' },
  { key: 'WE', label: 'Wed' },
  { key: 'TH', label: 'Thu' },
  { key: 'FR', label: 'Fri' },
  { key: 'SA', label: 'Sat' },
  { key: 'SU', label: 'Sun' },
];

function parseRRule(rrule: string): {
  freq: Freq;
  interval: number;
  byDay: string[];
  byMonthDay: number[];
  endType: EndType;
  count: number;
  until: string;
} {
  const result = {
    freq: 'WEEKLY' as Freq,
    interval: 1,
    byDay: [] as string[],
    byMonthDay: [] as number[],
    endType: 'never' as EndType,
    count: 10,
    until: '',
  };

  if (!rrule) return result;

  const parts = rrule.replace('RRULE:', '').split(';');
  for (const part of parts) {
    const [key, val] = part.split('=');
    switch (key) {
      case 'FREQ':
        result.freq = val as Freq;
        break;
      case 'INTERVAL':
        result.interval = parseInt(val) || 1;
        break;
      case 'BYDAY':
        result.byDay = val.split(',');
        break;
      case 'BYMONTHDAY':
        result.byMonthDay = val.split(',').map(Number);
        break;
      case 'COUNT':
        result.endType = 'count';
        result.count = parseInt(val) || 10;
        break;
      case 'UNTIL':
        result.endType = 'until';
        // Convert YYYYMMDD to YYYY-MM-DD
        if (val.length >= 8) {
          result.until = `${val.slice(0, 4)}-${val.slice(4, 6)}-${val.slice(6, 8)}`;
        }
        break;
    }
  }
  return result;
}

function buildRRule(state: {
  freq: Freq;
  interval: number;
  byDay: string[];
  byMonthDay: number[];
  endType: EndType;
  count: number;
  until: string;
}): string {
  const parts: string[] = [`FREQ=${state.freq}`];
  if (state.interval > 1) parts.push(`INTERVAL=${state.interval}`);
  if (state.freq === 'WEEKLY' && state.byDay.length > 0) {
    parts.push(`BYDAY=${state.byDay.join(',')}`);
  }
  if (state.freq === 'MONTHLY' && state.byMonthDay.length > 0) {
    parts.push(`BYMONTHDAY=${state.byMonthDay.join(',')}`);
  }
  if (state.endType === 'count') {
    parts.push(`COUNT=${state.count}`);
  } else if (state.endType === 'until' && state.until) {
    parts.push(`UNTIL=${state.until.replace(/-/g, '')}T235959Z`);
  }
  return parts.join(';');
}

export function RecurrenceEditor({ value, onChange }: Props) {
  const [state, setState] = useState(() => parseRRule(value));

  const updateAndEmit = useCallback(
    (updates: Partial<typeof state>) => {
      const newState = { ...state, ...updates };
      setState(newState);
      onChange(buildRRule(newState));
    },
    [state, onChange],
  );

  // Emit initial value if empty
  useEffect(() => {
    if (!value) {
      onChange(buildRRule(state));
    }
  }, []);

  return (
    <div className='space-y-3 p-3 bg-content2/50 rounded-lg'>
      <div className='flex items-center gap-3'>
        <span className='text-sm text-default-600'>Every</span>
        <Input
          type='number'
          min={1}
          max={99}
          value={String(state.interval)}
          onChange={(e) =>
            updateAndEmit({
              interval: Math.max(1, parseInt(e.target.value) || 1),
            })
          }
          className='w-20'
          size='sm'
        />
        <Select
          selectedKeys={[state.freq]}
          onSelectionChange={(keys) => {
            const freq = Array.from(keys)[0] as Freq;
            updateAndEmit({ freq, byDay: [], byMonthDay: [] });
          }}
          className='w-36'
          size='sm'
        >
          <SelectItem key='DAILY'>day(s)</SelectItem>
          <SelectItem key='WEEKLY'>week(s)</SelectItem>
          <SelectItem key='MONTHLY'>month(s)</SelectItem>
          <SelectItem key='YEARLY'>year(s)</SelectItem>
        </Select>
      </div>

      {/* BYDAY for weekly */}
      {state.freq === 'WEEKLY' && (
        <div>
          <p className='text-xs text-default-500 mb-1.5'>On days</p>
          <div className='flex gap-1'>
            {WEEKDAYS.map((wd) => {
              const selected = state.byDay.includes(wd.key);
              return (
                <button
                  key={wd.key}
                  onClick={() => {
                    const byDay = selected
                      ? state.byDay.filter((d) => d !== wd.key)
                      : [...state.byDay, wd.key];
                    updateAndEmit({ byDay });
                  }}
                  className={`w-9 h-9 rounded-full text-xs font-medium transition-colors ${
                    selected
                      ? 'bg-primary text-white'
                      : 'bg-content2 text-default-600 hover:bg-content3'
                  }`}
                >
                  {wd.label}
                </button>
              );
            })}
          </div>
        </div>
      )}

      {/* BYMONTHDAY for monthly */}
      {state.freq === 'MONTHLY' && (
        <div>
          <p className='text-xs text-default-500 mb-1.5'>On day of month</p>
          <Input
            type='number'
            min={-31}
            max={31}
            value={String(state.byMonthDay[0] || 1)}
            onChange={(e) =>
              updateAndEmit({
                byMonthDay: [parseInt(e.target.value) || 1],
              })
            }
            size='sm'
            className='w-24'
            description='-1 for last day'
          />
        </div>
      )}

      {/* End condition */}
      <div className='flex items-center gap-3'>
        <span className='text-sm text-default-600'>Ends</span>
        <Select
          selectedKeys={[state.endType]}
          onSelectionChange={(keys) => {
            updateAndEmit({ endType: Array.from(keys)[0] as EndType });
          }}
          className='w-32'
          size='sm'
        >
          <SelectItem key='never'>Never</SelectItem>
          <SelectItem key='count'>After</SelectItem>
          <SelectItem key='until'>On date</SelectItem>
        </Select>

        {state.endType === 'count' && (
          <div className='flex items-center gap-2'>
            <Input
              type='number'
              min={1}
              max={999}
              value={String(state.count)}
              onChange={(e) =>
                updateAndEmit({
                  count: Math.max(1, parseInt(e.target.value) || 1),
                })
              }
              size='sm'
              className='w-20'
            />
            <span className='text-sm text-default-600'>occurrences</span>
          </div>
        )}

        {state.endType === 'until' && (
          <Input
            type='date'
            value={state.until}
            onChange={(e) => updateAndEmit({ until: e.target.value })}
            size='sm'
            className='w-44'
          />
        )}
      </div>
    </div>
  );
}
