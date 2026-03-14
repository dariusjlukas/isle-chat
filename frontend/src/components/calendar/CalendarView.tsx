import { useState, useEffect, useCallback, useMemo } from 'react';
import { Button, Spinner, ButtonGroup } from '@heroui/react';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import {
  faChevronLeft,
  faChevronRight,
  faPlus,
  faShield,
  faCalendarDays,
  faCalendarWeek,
  faCalendar,
  faList,
} from '@fortawesome/free-solid-svg-icons';
import { useChatStore } from '../../stores/chatStore';
import * as api from '../../services/api';
import type { CalendarEvent } from '../../types';
import { MonthView } from './MonthView';
import { WeekView } from './WeekView';
import { DayView } from './DayView';
import { AgendaView } from './AgendaView';
import { EventModal } from './EventModal';
import { CalendarPermissions } from './CalendarPermissions';

type ViewMode = 'month' | 'week' | 'day' | 'agenda';

interface Props {
  spaceId: string;
}

function startOfMonth(d: Date): Date {
  return new Date(d.getFullYear(), d.getMonth(), 1);
}
function endOfMonth(d: Date): Date {
  return new Date(d.getFullYear(), d.getMonth() + 1, 0, 23, 59, 59);
}
function startOfWeek(d: Date): Date {
  const day = d.getDay();
  const diff = d.getDate() - day + (day === 0 ? -6 : 1); // Monday start
  return new Date(d.getFullYear(), d.getMonth(), diff);
}
function endOfWeek(d: Date): Date {
  const s = startOfWeek(d);
  return new Date(s.getFullYear(), s.getMonth(), s.getDate() + 6, 23, 59, 59);
}
function startOfDay(d: Date): Date {
  return new Date(d.getFullYear(), d.getMonth(), d.getDate());
}
function endOfDay(d: Date): Date {
  return new Date(d.getFullYear(), d.getMonth(), d.getDate(), 23, 59, 59);
}

function getViewRange(mode: ViewMode, date: Date): { start: Date; end: Date } {
  switch (mode) {
    case 'month': {
      // Extend to cover partial weeks at month boundaries
      const ms = startOfMonth(date);
      const me = endOfMonth(date);
      const s = startOfWeek(ms);
      const e = endOfWeek(me);
      return { start: s, end: e };
    }
    case 'week':
      return { start: startOfWeek(date), end: endOfWeek(date) };
    case 'day':
      return { start: startOfDay(date), end: endOfDay(date) };
    case 'agenda': {
      // Show 30 days from current date
      const s = startOfDay(date);
      const e = new Date(s);
      e.setDate(e.getDate() + 30);
      return { start: s, end: e };
    }
  }
}

function navigateDate(mode: ViewMode, date: Date, direction: number): Date {
  const d = new Date(date);
  switch (mode) {
    case 'month':
      d.setMonth(d.getMonth() + direction);
      break;
    case 'week':
      d.setDate(d.getDate() + 7 * direction);
      break;
    case 'day':
      d.setDate(d.getDate() + direction);
      break;
    case 'agenda':
      d.setDate(d.getDate() + 30 * direction);
      break;
  }
  return d;
}

function formatHeaderDate(mode: ViewMode, date: Date): string {
  switch (mode) {
    case 'month':
      return date.toLocaleDateString(undefined, {
        month: 'long',
        year: 'numeric',
      });
    case 'week': {
      const s = startOfWeek(date);
      const e = endOfWeek(date);
      const sStr = s.toLocaleDateString(undefined, {
        month: 'short',
        day: 'numeric',
      });
      const eStr = e.toLocaleDateString(undefined, {
        month: 'short',
        day: 'numeric',
        year: 'numeric',
      });
      return `${sStr} – ${eStr}`;
    }
    case 'day':
      return date.toLocaleDateString(undefined, {
        weekday: 'long',
        month: 'long',
        day: 'numeric',
        year: 'numeric',
      });
    case 'agenda':
      return 'Upcoming Events';
  }
}

export function CalendarView({ spaceId }: Props) {
  const user = useChatStore((s) => s.user);
  const spaces = useChatStore((s) => s.spaces);
  const space = spaces.find((s) => s.id === spaceId);

  const [viewMode, setViewMode] = useState<ViewMode>('month');
  const [currentDate, setCurrentDate] = useState(new Date());
  const [events, setEvents] = useState<CalendarEvent[]>([]);
  const [loading, setLoading] = useState(true);
  const [myPermission, setMyPermission] = useState('view');

  // Modal state
  const [selectedEvent, setSelectedEvent] = useState<CalendarEvent | null>(
    null,
  );
  const [createDate, setCreateDate] = useState<Date | null>(null);
  const [showPermissions, setShowPermissions] = useState(false);

  const range = useMemo(
    () => getViewRange(viewMode, currentDate),
    [viewMode, currentDate],
  );

  const loadEvents = useCallback(async () => {
    setLoading(true);
    try {
      const data = await api.listCalendarEvents(
        spaceId,
        range.start.toISOString(),
        range.end.toISOString(),
      );
      setEvents(data.events);
      setMyPermission(data.my_permission);
    } catch {
      // silently fail
    } finally {
      setLoading(false);
    }
  }, [spaceId, range.start.getTime(), range.end.getTime()]);

  useEffect(() => {
    loadEvents();
  }, [loadEvents]);

  const canEdit =
    myPermission === 'edit' ||
    myPermission === 'owner' ||
    user?.role === 'admin' ||
    user?.role === 'owner';
  const canManagePermissions =
    myPermission === 'owner' ||
    user?.role === 'admin' ||
    user?.role === 'owner';

  const handleDayClick = (date: Date) => {
    if (!canEdit || space?.is_archived) return;
    setCreateDate(date);
  };

  const handleEventClick = (event: CalendarEvent) => {
    setSelectedEvent(event);
  };

  const handleCloseModal = () => {
    setSelectedEvent(null);
    setCreateDate(null);
  };

  const handleSaved = () => {
    handleCloseModal();
    loadEvents();
  };

  return (
    <div className='flex-1 flex flex-col min-w-0 overflow-hidden bg-background'>
      {/* Header */}
      <div className='flex items-center justify-between px-4 py-3 border-b border-default-200'>
        <div className='flex items-center gap-3'>
          <Button
            isIconOnly
            variant='light'
            size='sm'
            onPress={() =>
              setCurrentDate(navigateDate(viewMode, currentDate, -1))
            }
          >
            <FontAwesomeIcon icon={faChevronLeft} />
          </Button>
          <Button
            variant='light'
            size='sm'
            onPress={() => setCurrentDate(new Date())}
          >
            Today
          </Button>
          <Button
            isIconOnly
            variant='light'
            size='sm'
            onPress={() =>
              setCurrentDate(navigateDate(viewMode, currentDate, 1))
            }
          >
            <FontAwesomeIcon icon={faChevronRight} />
          </Button>
          <h2 className='text-lg font-semibold text-foreground ml-2'>
            {formatHeaderDate(viewMode, currentDate)}
          </h2>
        </div>

        <div className='flex items-center gap-2'>
          <ButtonGroup size='sm' variant='flat'>
            <Button
              onPress={() => setViewMode('month')}
              color={viewMode === 'month' ? 'primary' : 'default'}
              title='Month view'
            >
              <FontAwesomeIcon icon={faCalendarDays} className='mr-1.5' />
              Month
            </Button>
            <Button
              onPress={() => setViewMode('week')}
              color={viewMode === 'week' ? 'primary' : 'default'}
              title='Week view'
            >
              <FontAwesomeIcon icon={faCalendarWeek} className='mr-1.5' />
              Week
            </Button>
            <Button
              onPress={() => setViewMode('day')}
              color={viewMode === 'day' ? 'primary' : 'default'}
              title='Day view'
            >
              <FontAwesomeIcon icon={faCalendar} className='mr-1.5' />
              Day
            </Button>
            <Button
              onPress={() => setViewMode('agenda')}
              color={viewMode === 'agenda' ? 'primary' : 'default'}
              title='Agenda view'
            >
              <FontAwesomeIcon icon={faList} className='mr-1.5' />
              Agenda
            </Button>
          </ButtonGroup>

          {canManagePermissions && (
            <Button
              isIconOnly
              variant='light'
              size='sm'
              onPress={() => setShowPermissions(true)}
              title='Calendar permissions'
            >
              <FontAwesomeIcon icon={faShield} />
            </Button>
          )}

          {canEdit && !space?.is_archived && (
            <Button
              color='primary'
              size='sm'
              onPress={() => setCreateDate(new Date())}
            >
              <FontAwesomeIcon icon={faPlus} className='mr-1.5' />
              New Event
            </Button>
          )}
        </div>
      </div>

      {/* Calendar body */}
      <div className='flex-1 overflow-auto relative'>
        {loading && events.length === 0 ? (
          <div className='flex items-center justify-center h-full'>
            <Spinner size='lg' />
          </div>
        ) : (
          <>
            {viewMode === 'month' && (
              <MonthView
                events={events}
                currentDate={currentDate}
                onDayClick={handleDayClick}
                onEventClick={handleEventClick}
              />
            )}
            {viewMode === 'week' && (
              <WeekView
                events={events}
                currentDate={currentDate}
                onTimeClick={handleDayClick}
                onEventClick={handleEventClick}
              />
            )}
            {viewMode === 'day' && (
              <DayView
                events={events}
                currentDate={currentDate}
                onTimeClick={handleDayClick}
                onEventClick={handleEventClick}
              />
            )}
            {viewMode === 'agenda' && (
              <AgendaView events={events} onEventClick={handleEventClick} />
            )}
          </>
        )}
      </div>

      {/* Event create/edit modal */}
      {(selectedEvent || createDate) && (
        <EventModal
          spaceId={spaceId}
          event={selectedEvent}
          initialDate={createDate}
          canEdit={canEdit}
          onClose={handleCloseModal}
          onSaved={handleSaved}
        />
      )}

      {/* Permissions modal */}
      {showPermissions && (
        <CalendarPermissions
          spaceId={spaceId}
          onClose={() => setShowPermissions(false)}
        />
      )}
    </div>
  );
}
