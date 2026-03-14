import { useState, useEffect } from 'react';
import {
  Modal,
  ModalContent,
  ModalHeader,
  ModalBody,
  ModalFooter,
  Button,
  Input,
  Textarea,
  Switch,
  Chip,
  Dropdown,
  DropdownTrigger,
  DropdownMenu,
  DropdownItem,
} from '@heroui/react';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import {
  faTrash,
  faLocationDot,
  faRepeat,
  faCheck,
  faQuestion,
  faXmark,
} from '@fortawesome/free-solid-svg-icons';
import * as api from '../../services/api';
import type { CalendarEvent, CalendarEventRsvp } from '../../types';
import { RecurrenceEditor } from './RecurrenceEditor';

const EVENT_COLORS = [
  { key: 'blue', label: 'Blue', class: 'bg-blue-500' },
  { key: 'red', label: 'Red', class: 'bg-red-500' },
  { key: 'green', label: 'Green', class: 'bg-green-500' },
  { key: 'purple', label: 'Purple', class: 'bg-purple-500' },
  { key: 'orange', label: 'Orange', class: 'bg-orange-500' },
  { key: 'pink', label: 'Pink', class: 'bg-pink-500' },
  { key: 'yellow', label: 'Yellow', class: 'bg-yellow-500' },
  { key: 'teal', label: 'Teal', class: 'bg-teal-500' },
];

interface Props {
  spaceId: string;
  event: CalendarEvent | null;
  initialDate: Date | null;
  canEdit: boolean;
  onClose: () => void;
  onSaved: () => void;
}

function toLocalDatetime(d: Date): string {
  const pad = (n: number) => String(n).padStart(2, '0');
  return `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())}T${pad(d.getHours())}:${pad(d.getMinutes())}`;
}

function toLocalDate(d: Date): string {
  const pad = (n: number) => String(n).padStart(2, '0');
  return `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())}`;
}

export function EventModal({
  spaceId,
  event,
  initialDate,
  canEdit,
  onClose,
  onSaved,
}: Props) {
  const isNew = !event;
  const isRecurringOccurrence = !!(event?.rrule && event?.occurrence_date);

  // Form state
  const [title, setTitle] = useState(event?.title || '');
  const [description, setDescription] = useState(event?.description || '');
  const [location, setLocation] = useState(event?.location || '');
  const [color, setColor] = useState(event?.color || 'blue');
  const [allDay, setAllDay] = useState(event?.all_day || false);
  const [rrule, setRrule] = useState(event?.rrule || '');
  const [showRecurrence, setShowRecurrence] = useState(!!event?.rrule);

  // Date/time state
  const [startDate, setStartDate] = useState('');
  const [startTime, setStartTime] = useState('');
  const [endDate, setEndDate] = useState('');
  const [endTime, setEndTime] = useState('');

  // RSVP state
  const [myRsvp, setMyRsvp] = useState(event?.my_rsvp || '');
  const [rsvps, setRsvps] = useState<CalendarEventRsvp[]>([]);
  const [saving, setSaving] = useState(false);

  useEffect(() => {
    if (event) {
      const s = new Date(event.start_time);
      const e = new Date(event.end_time);
      setStartDate(toLocalDate(s));
      setStartTime(toLocalDatetime(s).split('T')[1]);
      setEndDate(toLocalDate(e));
      setEndTime(toLocalDatetime(e).split('T')[1]);
      // Load RSVPs
      const occDate = event.occurrence_date || '1970-01-01 00:00:00+00';
      api
        .getEventRsvps(spaceId, event.id, occDate)
        .then((data) => {
          setRsvps(data.rsvps);
        })
        .catch(() => {});
    } else if (initialDate) {
      const s = new Date(initialDate);
      s.setMinutes(0);
      s.setSeconds(0);
      if (s.getHours() === 0) s.setHours(9);
      const e = new Date(s);
      e.setHours(e.getHours() + 1);
      setStartDate(toLocalDate(s));
      setStartTime(toLocalDatetime(s).split('T')[1]);
      setEndDate(toLocalDate(e));
      setEndTime(toLocalDatetime(e).split('T')[1]);
    }
  }, [event, initialDate, spaceId]);

  const buildStartTime = (): string => {
    if (allDay) return `${startDate}T00:00:00Z`;
    return `${startDate}T${startTime}:00Z`;
  };

  const buildEndTime = (): string => {
    if (allDay) return `${endDate}T23:59:59Z`;
    return `${endDate}T${endTime}:00Z`;
  };

  const handleSave = async () => {
    if (!title.trim()) return;
    setSaving(true);
    try {
      if (isNew) {
        await api.createCalendarEvent(spaceId, {
          title: title.trim(),
          description,
          location,
          color,
          start_time: buildStartTime(),
          end_time: buildEndTime(),
          all_day: allDay,
          rrule: showRecurrence ? rrule : '',
        });
      } else if (event) {
        await api.updateCalendarEvent(spaceId, event.id, {
          title: title.trim(),
          description,
          location,
          color,
          start_time: buildStartTime(),
          end_time: buildEndTime(),
          all_day: allDay,
          rrule: showRecurrence ? rrule : '',
        });
      }
      onSaved();
    } catch {
      // error handling
    } finally {
      setSaving(false);
    }
  };

  const handleDelete = async () => {
    if (!event) return;
    setSaving(true);
    try {
      await api.deleteCalendarEvent(spaceId, event.id);
      onSaved();
    } catch {
      // error
    } finally {
      setSaving(false);
    }
  };

  const handleDeleteOccurrence = async () => {
    if (!event?.occurrence_date) return;
    setSaving(true);
    try {
      await api.createEventException(spaceId, event.id, {
        original_date: event.occurrence_date,
        is_deleted: true,
      });
      onSaved();
    } catch {
      // error
    } finally {
      setSaving(false);
    }
  };

  const handleRsvp = async (status: 'yes' | 'no' | 'maybe') => {
    if (!event) return;
    const occDate = event.occurrence_date || undefined;
    try {
      await api.setEventRsvp(spaceId, event.id, status, occDate);
      setMyRsvp(status);
      // Reload RSVPs
      const data = await api.getEventRsvps(
        spaceId,
        event.id,
        occDate || '1970-01-01 00:00:00+00',
      );
      setRsvps(data.rsvps);
    } catch {
      // error
    }
  };

  const colorInfo =
    EVENT_COLORS.find((c) => c.key === color) || EVENT_COLORS[0];

  return (
    <Modal isOpen onClose={onClose} size='lg' scrollBehavior='inside'>
      <ModalContent>
        <ModalHeader className='flex items-center gap-2'>
          <div className={`w-3 h-3 rounded-full ${colorInfo.class}`} />
          {isNew ? 'New Event' : canEdit ? 'Edit Event' : 'Event Details'}
        </ModalHeader>
        <ModalBody>
          {canEdit ? (
            <>
              <Input
                label='Title'
                variant='bordered'
                value={title}
                onValueChange={setTitle}
                autoFocus
                isRequired
                maxLength={255}
              />
              <Textarea
                label='Description'
                variant='bordered'
                value={description}
                onValueChange={setDescription}
                minRows={2}
                maxRows={5}
              />
              <Input
                label='Location'
                variant='bordered'
                value={location}
                onValueChange={setLocation}
                startContent={
                  <FontAwesomeIcon
                    icon={faLocationDot}
                    className='text-default-400'
                  />
                }
              />

              {/* Color picker */}
              <div>
                <p className='text-sm text-default-500 mb-2'>Color</p>
                <div className='flex gap-2'>
                  {EVENT_COLORS.map((c) => (
                    <button
                      key={c.key}
                      onClick={() => setColor(c.key)}
                      className={`w-6 h-6 rounded-full ${c.class} transition-transform ${
                        color === c.key
                          ? 'ring-2 ring-offset-2 ring-primary scale-110'
                          : 'hover:scale-110'
                      }`}
                      title={c.label}
                    />
                  ))}
                </div>
              </div>

              {/* All-day toggle */}
              <Switch isSelected={allDay} onValueChange={setAllDay}>
                All day
              </Switch>

              {/* Date/time pickers */}
              <div className='grid grid-cols-2 gap-3'>
                <Input
                  type='date'
                  label='Start date'
                  variant='bordered'
                  value={startDate}
                  onChange={(e) => setStartDate(e.target.value)}
                />
                {!allDay && (
                  <Input
                    type='time'
                    label='Start time'
                    variant='bordered'
                    value={startTime}
                    onChange={(e) => setStartTime(e.target.value)}
                  />
                )}
                <Input
                  type='date'
                  label='End date'
                  variant='bordered'
                  value={endDate}
                  onChange={(e) => setEndDate(e.target.value)}
                />
                {!allDay && (
                  <Input
                    type='time'
                    label='End time'
                    variant='bordered'
                    value={endTime}
                    onChange={(e) => setEndTime(e.target.value)}
                  />
                )}
              </div>

              {/* Recurrence */}
              <div>
                <Button
                  variant='flat'
                  size='sm'
                  onPress={() => setShowRecurrence(!showRecurrence)}
                  startContent={<FontAwesomeIcon icon={faRepeat} />}
                >
                  {showRecurrence ? 'Remove recurrence' : 'Add recurrence'}
                </Button>
                {showRecurrence && (
                  <div className='mt-3'>
                    <RecurrenceEditor value={rrule} onChange={setRrule} />
                  </div>
                )}
              </div>
            </>
          ) : (
            /* Read-only view */
            <div className='space-y-3'>
              <h3 className='text-xl font-semibold'>{event?.title}</h3>
              {event?.description && (
                <p className='text-default-600 whitespace-pre-wrap'>
                  {event.description}
                </p>
              )}
              {event?.location && (
                <p className='text-default-500'>
                  <FontAwesomeIcon icon={faLocationDot} className='mr-2' />
                  {event.location}
                </p>
              )}
              <div className='text-default-500 text-sm'>
                <p>
                  {event?.all_day
                    ? new Date(event.start_time).toLocaleDateString()
                    : new Date(event?.start_time || '').toLocaleString()}
                  {' – '}
                  {event?.all_day
                    ? new Date(event.end_time).toLocaleDateString()
                    : new Date(event?.end_time || '').toLocaleString()}
                </p>
                {event?.rrule && (
                  <p className='mt-1'>
                    <FontAwesomeIcon icon={faRepeat} className='mr-1' />
                    Recurring event
                  </p>
                )}
              </div>
              <p className='text-xs text-default-400'>
                Created by {event?.created_by_username}
              </p>
            </div>
          )}

          {/* RSVP section (always visible for existing events) */}
          {event && (
            <div className='border-t border-default-200 pt-3 mt-3'>
              <p className='text-sm font-medium mb-2'>RSVP</p>
              <div className='flex gap-2 mb-3'>
                <Button
                  size='sm'
                  variant={myRsvp === 'yes' ? 'solid' : 'flat'}
                  color={myRsvp === 'yes' ? 'success' : 'default'}
                  onPress={() => handleRsvp('yes')}
                  startContent={<FontAwesomeIcon icon={faCheck} />}
                >
                  Yes
                </Button>
                <Button
                  size='sm'
                  variant={myRsvp === 'maybe' ? 'solid' : 'flat'}
                  color={myRsvp === 'maybe' ? 'warning' : 'default'}
                  onPress={() => handleRsvp('maybe')}
                  startContent={<FontAwesomeIcon icon={faQuestion} />}
                >
                  Maybe
                </Button>
                <Button
                  size='sm'
                  variant={myRsvp === 'no' ? 'solid' : 'flat'}
                  color={myRsvp === 'no' ? 'danger' : 'default'}
                  onPress={() => handleRsvp('no')}
                  startContent={<FontAwesomeIcon icon={faXmark} />}
                >
                  No
                </Button>
              </div>
              {rsvps.length > 0 && (
                <div className='space-y-1'>
                  {rsvps.map((r) => (
                    <div
                      key={r.user_id}
                      className='flex items-center justify-between text-sm'
                    >
                      <span className='text-default-600'>
                        {r.display_name || r.username}
                      </span>
                      <Chip
                        size='sm'
                        color={
                          r.status === 'yes'
                            ? 'success'
                            : r.status === 'maybe'
                              ? 'warning'
                              : 'danger'
                        }
                        variant='flat'
                      >
                        {r.status}
                      </Chip>
                    </div>
                  ))}
                </div>
              )}
            </div>
          )}
        </ModalBody>
        <ModalFooter>
          {event && canEdit && (
            <div className='mr-auto'>
              {isRecurringOccurrence ? (
                <Dropdown>
                  <DropdownTrigger>
                    <Button
                      color='danger'
                      variant='light'
                      size='sm'
                      startContent={<FontAwesomeIcon icon={faTrash} />}
                    >
                      Delete
                    </Button>
                  </DropdownTrigger>
                  <DropdownMenu>
                    <DropdownItem
                      key='occurrence'
                      onPress={handleDeleteOccurrence}
                    >
                      This occurrence only
                    </DropdownItem>
                    <DropdownItem
                      key='all'
                      className='text-danger'
                      onPress={handleDelete}
                    >
                      All events in series
                    </DropdownItem>
                  </DropdownMenu>
                </Dropdown>
              ) : (
                <Button
                  color='danger'
                  variant='light'
                  size='sm'
                  onPress={handleDelete}
                  isLoading={saving}
                  startContent={<FontAwesomeIcon icon={faTrash} />}
                >
                  Delete
                </Button>
              )}
            </div>
          )}
          <Button variant='light' onPress={onClose}>
            {canEdit ? 'Cancel' : 'Close'}
          </Button>
          {canEdit && (
            <Button
              color='primary'
              onPress={handleSave}
              isLoading={saving}
              isDisabled={!title.trim()}
            >
              {isNew ? 'Create' : 'Save'}
            </Button>
          )}
        </ModalFooter>
      </ModalContent>
    </Modal>
  );
}
