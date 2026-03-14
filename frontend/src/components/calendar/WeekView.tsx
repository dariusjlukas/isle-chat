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

function startOfWeek(d: Date): Date {
  const day = d.getDay();
  const diff = d.getDate() - day + (day === 0 ? -6 : 1);
  return new Date(d.getFullYear(), d.getMonth(), diff);
}

function isSameDay(a: Date, b: Date): boolean {
  return (
    a.getFullYear() === b.getFullYear() &&
    a.getMonth() === b.getMonth() &&
    a.getDate() === b.getDate()
  );
}

const HOURS = Array.from({ length: 24 }, (_, i) => i);
const HOUR_HEIGHT = 60; // px per hour

export function WeekView({
  events,
  currentDate,
  onTimeClick,
  onEventClick,
}: Props) {
  const today = new Date();
  const weekStart = startOfWeek(currentDate);

  const days = Array.from({ length: 7 }, (_, i) => {
    const d = new Date(weekStart);
    d.setDate(d.getDate() + i);
    return d;
  });

  // Split events into all-day and timed
  const allDayEvents: CalendarEvent[] = [];
  const timedEvents: CalendarEvent[] = [];
  for (const ev of events) {
    if (ev.all_day) {
      allDayEvents.push(ev);
    } else {
      timedEvents.push(ev);
    }
  }

  // Index timed events by day
  const timedByDay = new Map<string, CalendarEvent[]>();
  for (const ev of timedEvents) {
    const start = new Date(ev.start_time);
    const key = `${start.getFullYear()}-${start.getMonth()}-${start.getDate()}`;
    const list = timedByDay.get(key) || [];
    list.push(ev);
    timedByDay.set(key, list);
  }

  return (
    <div className='flex flex-col h-full'>
      {/* All-day events bar */}
      {allDayEvents.length > 0 && (
        <div className='border-b border-default-200'>
          <div className='grid grid-cols-[60px_repeat(7,1fr)]'>
            <div className='text-xs text-default-400 p-1 text-right pr-2'>
              All day
            </div>
            {days.map((day) => {
              const dayAllDay = allDayEvents.filter((ev) => {
                const s = new Date(ev.start_time);
                const e = new Date(ev.end_time);
                return (
                  day >= new Date(s.getFullYear(), s.getMonth(), s.getDate()) &&
                  day <= new Date(e.getFullYear(), e.getMonth(), e.getDate())
                );
              });
              return (
                <div
                  key={day.getTime()}
                  className='p-0.5 min-h-[28px] border-l border-default-100'
                >
                  {dayAllDay.map((ev) => (
                    <button
                      key={ev.id}
                      onClick={() => onEventClick(ev)}
                      className={`w-full text-left text-[11px] px-1 py-0.5 rounded truncate mb-0.5 ${
                        ALL_DAY_MAP[ev.color] || ALL_DAY_MAP.blue
                      }`}
                    >
                      {ev.title}
                    </button>
                  ))}
                </div>
              );
            })}
          </div>
        </div>
      )}

      {/* Time grid (headers + hours in same scroll container) */}
      <div className='flex-1 overflow-y-auto'>
        {/* Day headers - sticky inside scroll container so columns align */}
        <div className='grid grid-cols-[60px_repeat(7,1fr)] border-b border-default-200 sticky top-0 z-10 bg-background'>
          <div />
          {days.map((day) => {
            const isToday = isSameDay(day, today);
            return (
              <div
                key={day.getTime()}
                className='text-center py-2 border-l border-default-100'
              >
                <div className='text-xs text-default-400'>
                  {day.toLocaleDateString(undefined, { weekday: 'short' })}
                </div>
                <div
                  className={`text-sm font-medium w-7 h-7 flex items-center justify-center rounded-full mx-auto ${
                    isToday ? 'bg-primary text-white' : ''
                  }`}
                >
                  {day.getDate()}
                </div>
              </div>
            );
          })}
        </div>

        <div className='grid grid-cols-[60px_repeat(7,1fr)] relative'>
          {/* Time labels */}
          <div>
            {HOURS.map((h) => (
              <div
                key={h}
                style={{ height: HOUR_HEIGHT }}
                className='text-xs text-default-400 text-right pr-2 pt-0 -mt-2 relative'
              >
                {h === 0
                  ? ''
                  : new Date(2000, 0, 1, h).toLocaleTimeString(undefined, {
                      hour: 'numeric',
                    })}
              </div>
            ))}
          </div>

          {/* Day columns */}
          {days.map((day) => {
            const key = `${day.getFullYear()}-${day.getMonth()}-${day.getDate()}`;
            const dayEvents = timedByDay.get(key) || [];

            return (
              <div
                key={day.getTime()}
                className='border-l border-default-100 relative'
              >
                {/* Hour lines */}
                {HOURS.map((h) => (
                  <div
                    key={h}
                    style={{ height: HOUR_HEIGHT }}
                    className='border-b border-default-100 cursor-pointer hover:bg-content2/30'
                    onClick={() => {
                      const d = new Date(day);
                      d.setHours(h);
                      onTimeClick(d);
                    }}
                  />
                ))}

                {/* Event blocks */}
                {dayEvents.map((ev) => {
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
                      className={`absolute left-0.5 right-0.5 rounded px-1 py-0.5 text-white text-[11px] leading-tight overflow-hidden cursor-pointer ${
                        COLOR_MAP[ev.color] || COLOR_MAP.blue
                      }`}
                      style={{
                        top: `${top}px`,
                        height: `${Math.max(height, 18)}px`,
                      }}
                    >
                      <div className='font-medium truncate'>{ev.title}</div>
                      {height > 30 && (
                        <div className='opacity-80 truncate'>
                          {start.toLocaleTimeString(undefined, {
                            hour: 'numeric',
                            minute: '2-digit',
                          })}
                        </div>
                      )}
                    </button>
                  );
                })}
              </div>
            );
          })}
        </div>
      </div>
    </div>
  );
}
