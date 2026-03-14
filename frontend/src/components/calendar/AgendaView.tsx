import { useMemo } from 'react';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import { faLocationDot, faRepeat } from '@fortawesome/free-solid-svg-icons';
import type { CalendarEvent } from '../../types';

const COLOR_MAP: Record<string, string> = {
  blue: 'bg-blue-500',
  red: 'bg-red-500',
  green: 'bg-green-500',
  purple: 'bg-purple-500',
  orange: 'bg-orange-500',
  pink: 'bg-pink-500',
  yellow: 'bg-yellow-500',
  teal: 'bg-teal-500',
};

interface Props {
  events: CalendarEvent[];
  onEventClick: (event: CalendarEvent) => void;
}

interface DayGroup {
  date: Date;
  label: string;
  events: CalendarEvent[];
}

export function AgendaView({ events, onEventClick }: Props) {
  const grouped = useMemo(() => {
    const groups = new Map<string, DayGroup>();

    for (const ev of events) {
      const start = new Date(ev.start_time);
      const key = `${start.getFullYear()}-${start.getMonth()}-${start.getDate()}`;
      if (!groups.has(key)) {
        groups.set(key, {
          date: new Date(
            start.getFullYear(),
            start.getMonth(),
            start.getDate(),
          ),
          label: start.toLocaleDateString(undefined, {
            weekday: 'long',
            month: 'long',
            day: 'numeric',
            year: 'numeric',
          }),
          events: [],
        });
      }
      groups.get(key)!.events.push(ev);
    }

    return Array.from(groups.values()).sort(
      (a, b) => a.date.getTime() - b.date.getTime(),
    );
  }, [events]);

  if (events.length === 0) {
    return (
      <div className='flex items-center justify-center h-full text-default-400'>
        No upcoming events
      </div>
    );
  }

  const today = new Date();
  const isToday = (d: Date) =>
    d.getFullYear() === today.getFullYear() &&
    d.getMonth() === today.getMonth() &&
    d.getDate() === today.getDate();

  return (
    <div className='max-w-3xl mx-auto p-4 space-y-6'>
      {grouped.map((group) => (
        <div key={group.date.getTime()}>
          <h3
            className={`text-sm font-semibold mb-2 ${
              isToday(group.date) ? 'text-primary' : 'text-default-500'
            }`}
          >
            {isToday(group.date) ? 'Today' : group.label}
          </h3>
          <div className='space-y-2'>
            {group.events.map((ev) => {
              const start = new Date(ev.start_time);
              const end = new Date(ev.end_time);
              return (
                <button
                  key={`${ev.id}-${ev.occurrence_date || ''}`}
                  onClick={() => onEventClick(ev)}
                  className='w-full text-left flex items-start gap-3 p-3 rounded-lg hover:bg-content2/50 transition-colors border border-default-200'
                >
                  <div
                    className={`w-1 self-stretch rounded-full flex-shrink-0 ${
                      COLOR_MAP[ev.color] || COLOR_MAP.blue
                    }`}
                  />
                  <div className='flex-1 min-w-0'>
                    <div className='font-medium text-foreground truncate'>
                      {ev.title}
                    </div>
                    <div className='text-sm text-default-500 mt-0.5'>
                      {ev.all_day
                        ? 'All day'
                        : `${start.toLocaleTimeString(undefined, {
                            hour: 'numeric',
                            minute: '2-digit',
                          })} – ${end.toLocaleTimeString(undefined, {
                            hour: 'numeric',
                            minute: '2-digit',
                          })}`}
                    </div>
                    {ev.location && (
                      <div className='text-xs text-default-400 mt-1 truncate'>
                        <FontAwesomeIcon
                          icon={faLocationDot}
                          className='mr-1'
                        />
                        {ev.location}
                      </div>
                    )}
                    {ev.rrule && (
                      <div className='text-xs text-default-400 mt-0.5'>
                        <FontAwesomeIcon icon={faRepeat} className='mr-1' />
                        Recurring
                      </div>
                    )}
                  </div>
                </button>
              );
            })}
          </div>
        </div>
      ))}
    </div>
  );
}
