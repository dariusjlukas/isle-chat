import type { CalendarEvent } from '../../types';

const HOVER_MAP: Record<string, string> = {
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
  onDayClick: (date: Date) => void;
  onEventClick: (event: CalendarEvent) => void;
}

function isSameDay(a: Date, b: Date): boolean {
  return (
    a.getFullYear() === b.getFullYear() &&
    a.getMonth() === b.getMonth() &&
    a.getDate() === b.getDate()
  );
}

function startOfWeek(d: Date): Date {
  const day = d.getDay();
  const diff = d.getDate() - day + (day === 0 ? -6 : 1);
  return new Date(d.getFullYear(), d.getMonth(), diff);
}

export function MonthView({
  events,
  currentDate,
  onDayClick,
  onEventClick,
}: Props) {
  const today = new Date();
  const month = currentDate.getMonth();
  const year = currentDate.getFullYear();
  const monthStart = new Date(year, month, 1);
  const monthEnd = new Date(year, month + 1, 0);

  // Build grid of weeks
  const weeks = (() => {
    const result: Date[][] = [];
    let ws = startOfWeek(monthStart);

    while (ws <= monthEnd || result.length < 5) {
      const week: Date[] = [];
      for (let i = 0; i < 7; i++) {
        const d = new Date(ws);
        d.setDate(d.getDate() + i);
        week.push(d);
      }
      result.push(week);
      ws = new Date(ws);
      ws.setDate(ws.getDate() + 7);
      if (result.length >= 6) break;
    }
    return result;
  })();

  // Index events by day key
  const eventsByDay = (() => {
    const map = new Map<string, CalendarEvent[]>();
    for (const ev of events) {
      const start = new Date(ev.start_time);
      const end = new Date(ev.end_time);
      // For multi-day events, add to each day
      const d = new Date(start);
      while (d <= end) {
        const key = `${d.getFullYear()}-${d.getMonth()}-${d.getDate()}`;
        const list = map.get(key) || [];
        list.push(ev);
        map.set(key, list);
        d.setDate(d.getDate() + 1);
        if (ev.all_day) {
          d.setHours(0, 0, 0, 0);
        }
      }
    }
    return map;
  })();

  const dayNames = ['Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat', 'Sun'];

  return (
    <div className='flex flex-col h-full'>
      {/* Day headers */}
      <div className='grid grid-cols-7 border-b border-default-200'>
        {dayNames.map((name) => (
          <div
            key={name}
            className='text-center text-xs font-medium text-default-500 py-2'
          >
            {name}
          </div>
        ))}
      </div>

      {/* Weeks */}
      <div className='flex-1 grid auto-rows-[minmax(100px,1fr)]'>
        {weeks.map((week, wi) => (
          <div
            key={wi}
            className='grid grid-cols-7 border-b border-default-100'
          >
            {week.map((day) => {
              const key = `${day.getFullYear()}-${day.getMonth()}-${day.getDate()}`;
              const dayEvents = eventsByDay.get(key) || [];
              const isCurrentMonth = day.getMonth() === currentDate.getMonth();
              const isToday = isSameDay(day, today);
              const MAX_VISIBLE = 3;

              return (
                <div
                  key={key}
                  className={`border-r border-default-100 last:border-r-0 p-1 cursor-pointer hover:bg-content2/50 transition-colors ${
                    !isCurrentMonth ? 'opacity-40' : ''
                  }`}
                  onClick={() => onDayClick(day)}
                >
                  <div
                    className={`text-xs font-medium mb-0.5 w-6 h-6 flex items-center justify-center rounded-full ${
                      isToday ? 'bg-primary text-white' : 'text-default-600'
                    }`}
                  >
                    {day.getDate()}
                  </div>
                  <div className='space-y-0.5'>
                    {dayEvents.slice(0, MAX_VISIBLE).map((ev, i) => (
                      <button
                        key={`${ev.id}-${i}`}
                        onClick={(e) => {
                          e.stopPropagation();
                          onEventClick(ev);
                        }}
                        className={`relative z-10 w-full text-left text-[11px] leading-tight px-1 py-0.5 rounded truncate ${
                          HOVER_MAP[ev.color] || HOVER_MAP.blue
                        }`}
                      >
                        {!ev.all_day && (
                          <span className='font-medium mr-0.5'>
                            {new Date(ev.start_time).toLocaleTimeString(
                              undefined,
                              { hour: 'numeric', minute: '2-digit' },
                            )}
                          </span>
                        )}
                        {ev.title}
                      </button>
                    ))}
                    {dayEvents.length > MAX_VISIBLE && (
                      <p className='text-[10px] text-default-400 px-1'>
                        +{dayEvents.length - MAX_VISIBLE} more
                      </p>
                    )}
                  </div>
                </div>
              );
            })}
          </div>
        ))}
      </div>
    </div>
  );
}
