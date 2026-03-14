import type { CalendarEvent } from '../../types';

const COLOR_MAP: Record<string, string> = {
  blue: 'bg-blue-500/80',
  red: 'bg-red-500/80',
  green: 'bg-green-500/80',
  purple: 'bg-purple-500/80',
  orange: 'bg-orange-500/80',
  pink: 'bg-pink-500/80',
  yellow: 'bg-yellow-500/80',
  teal: 'bg-teal-500/80',
};

const ALL_DAY_MAP: Record<string, string> = {
  blue: 'bg-blue-500/20 text-blue-700 dark:text-blue-300',
  red: 'bg-red-500/20 text-red-700 dark:text-red-300',
  green: 'bg-green-500/20 text-green-700 dark:text-green-300',
  purple: 'bg-purple-500/20 text-purple-700 dark:text-purple-300',
  orange: 'bg-orange-500/20 text-orange-700 dark:text-orange-300',
  pink: 'bg-pink-500/20 text-pink-700 dark:text-pink-300',
  yellow: 'bg-yellow-500/20 text-yellow-700 dark:text-yellow-300',
  teal: 'bg-teal-500/20 text-teal-700 dark:text-teal-300',
};

interface Props {
  events: CalendarEvent[];
  currentDate: Date;
  onTimeClick: (date: Date) => void;
  onEventClick: (event: CalendarEvent) => void;
}

function isSameDay(a: Date, b: Date): boolean {
  return (
    a.getFullYear() === b.getFullYear() &&
    a.getMonth() === b.getMonth() &&
    a.getDate() === b.getDate()
  );
}

const HOURS = Array.from({ length: 24 }, (_, i) => i);
const HOUR_HEIGHT = 60;

export function DayView({
  events,
  currentDate,
  onTimeClick,
  onEventClick,
}: Props) {
  const dayStart = new Date(
    currentDate.getFullYear(),
    currentDate.getMonth(),
    currentDate.getDate(),
  );

  const allDayEvents: CalendarEvent[] = [];
  const timedEvents: CalendarEvent[] = [];
  for (const ev of events) {
    const s = new Date(ev.start_time);
    const e = new Date(ev.end_time);
    const inDay =
      isSameDay(s, dayStart) ||
      isSameDay(e, dayStart) ||
      (s < dayStart && e > dayStart);
    if (!inDay) continue;
    if (ev.all_day) {
      allDayEvents.push(ev);
    } else {
      timedEvents.push(ev);
    }
  }

  return (
    <div className='flex flex-col h-full'>
      {/* All-day events */}
      {allDayEvents.length > 0 && (
        <div className='border-b border-default-200 p-2 space-y-1'>
          <p className='text-xs text-default-400 mb-1'>All day</p>
          {allDayEvents.map((ev) => (
            <button
              key={ev.id}
              onClick={() => onEventClick(ev)}
              className={`w-full text-left text-sm px-2 py-1 rounded ${
                ALL_DAY_MAP[ev.color] || ALL_DAY_MAP.blue
              }`}
            >
              {ev.title}
            </button>
          ))}
        </div>
      )}

      {/* Time grid */}
      <div className='flex-1 overflow-y-auto'>
        <div className='grid grid-cols-[60px_1fr] relative'>
          {/* Time labels */}
          <div>
            {HOURS.map((h) => (
              <div
                key={h}
                style={{ height: HOUR_HEIGHT }}
                className='text-xs text-default-400 text-right pr-2 -mt-2 relative'
              >
                {h === 0
                  ? ''
                  : new Date(2000, 0, 1, h).toLocaleTimeString(undefined, {
                      hour: 'numeric',
                    })}
              </div>
            ))}
          </div>

          {/* Day column */}
          <div className='border-l border-default-100 relative'>
            {HOURS.map((h) => (
              <div
                key={h}
                style={{ height: HOUR_HEIGHT }}
                className='border-b border-default-100 cursor-pointer hover:bg-content2/30'
                onClick={() => {
                  const d = new Date(dayStart);
                  d.setHours(h);
                  onTimeClick(d);
                }}
              />
            ))}

            {/* Event blocks */}
            {timedEvents.map((ev) => {
              const start = new Date(ev.start_time);
              const end = new Date(ev.end_time);
              const startMin = start.getHours() * 60 + start.getMinutes();
              const endMin = end.getHours() * 60 + end.getMinutes();
              const duration = Math.max(endMin - startMin, 15);
              const top = (startMin / 60) * HOUR_HEIGHT;
              const height = (duration / 60) * HOUR_HEIGHT;

              return (
                <button
                  key={`${ev.id}-${ev.occurrence_date || ''}`}
                  onClick={(e) => {
                    e.stopPropagation();
                    onEventClick(ev);
                  }}
                  className={`absolute left-1 right-1 rounded px-2 py-1 text-white text-sm overflow-hidden cursor-pointer ${
                    COLOR_MAP[ev.color] || COLOR_MAP.blue
                  }`}
                  style={{
                    top: `${top}px`,
                    height: `${Math.max(height, 20)}px`,
                  }}
                >
                  <div className='font-medium truncate'>{ev.title}</div>
                  {height > 35 && (
                    <div className='opacity-80 text-xs'>
                      {start.toLocaleTimeString(undefined, {
                        hour: 'numeric',
                        minute: '2-digit',
                      })}{' '}
                      –{' '}
                      {end.toLocaleTimeString(undefined, {
                        hour: 'numeric',
                        minute: '2-digit',
                      })}
                    </div>
                  )}
                  {height > 55 && ev.location && (
                    <div className='opacity-70 text-xs truncate mt-0.5'>
                      {ev.location}
                    </div>
                  )}
                </button>
              );
            })}
          </div>
        </div>
      </div>
    </div>
  );
}
