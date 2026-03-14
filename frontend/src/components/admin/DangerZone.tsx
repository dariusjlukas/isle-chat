import { useState } from 'react';
import { Button } from '@heroui/react';
import { useChatStore } from '../../stores/chatStore';
import * as api from '../../services/api';

function EmergencyButton({
  coverOpen,
  setCoverOpen,
  onPress,
  buttonLabel,
  plateLabel,
  plateLabelOpen,
}: {
  coverOpen: boolean;
  setCoverOpen: (open: boolean) => void;
  onPress: () => void;
  buttonLabel: string;
  plateLabel: string;
  plateLabelOpen: string;
}) {
  return (
    <div className='flex justify-center pt-40'>
      <div className='relative w-[136px]'>
        {/* Transparent hinged cover — swings outward and up */}
        <div
          className='absolute inset-0 z-10 cursor-pointer'
          style={{
            transformOrigin: 'top center',
            transform: coverOpen
              ? 'perspective(600px) rotateX(110deg)'
              : 'perspective(600px) rotateX(0deg)',
            transition: 'transform 0.5s cubic-bezier(0.4, 0, 0.2, 1)',
            transformStyle: 'preserve-3d',
          }}
          onClick={() => setCoverOpen(!coverOpen)}
        >
          {/* Front face (outside of cover — visible when closed) */}
          <div
            className='absolute inset-0 rounded-xl
              flex items-end justify-center pb-3
              hover:brightness-110 transition-[filter]'
            style={{
              background:
                'linear-gradient(180deg, rgba(250,204,21,0.25) 0%, rgba(250,204,21,0.12) 100%)',
              border: '2px solid rgba(250,204,21,0.4)',
              boxShadow:
                '0 4px 12px rgba(0,0,0,0.15), inset 0 1px 1px rgba(255,255,255,0.2)',
              backdropFilter: 'blur(1px)',
              backfaceVisibility: 'hidden',
            }}
          >
            <span className='text-[10px] font-semibold uppercase tracking-wider text-amber-500/80 select-none'>
              Lift cover to arm
            </span>
          </div>
          {/* Back face (underside of cover — visible when open) */}
          <div
            className='absolute inset-0 rounded-xl'
            style={{
              background:
                'linear-gradient(0deg, rgba(250,204,21,0.18) 0%, rgba(250,204,21,0.08) 100%)',
              border: '2px solid rgba(250,204,21,0.3)',
              boxShadow:
                'inset 0 2px 8px rgba(0,0,0,0.2), inset 0 -1px 1px rgba(255,255,255,0.1)',
              backfaceVisibility: 'hidden',
              transform: 'rotateX(180deg)',
            }}
          >
            {/* Inner ridges / texture of the plastic underside */}
            <div
              className='absolute inset-2 rounded-lg'
              style={{
                border: '1px solid rgba(250,204,21,0.15)',
                background:
                  'linear-gradient(180deg, rgba(0,0,0,0.05) 0%, rgba(250,204,21,0.06) 100%)',
              }}
            />
            <div className='absolute inset-0 flex items-start justify-center pt-2.5'>
              <span className='text-[9px] font-semibold uppercase tracking-wider text-amber-400/50 select-none'>
                Click to close
              </span>
            </div>
          </div>
        </div>

        {/* Yellow/black hazard base plate */}
        <div
          className='rounded-xl p-3 pb-4'
          style={{
            background:
              'repeating-linear-gradient(-45deg, #eab308, #eab308 8px, #1a1a1a 8px, #1a1a1a 16px)',
          }}
        >
          {/* Inner dark housing */}
          <div className='bg-neutral-800 rounded-lg p-3 flex flex-col items-center gap-2'>
            {/* The red button */}
            <button
              disabled={!coverOpen}
              onClick={onPress}
              className='w-20 h-20 rounded-full border-4 border-red-900/80 cursor-pointer
                transition-all duration-150 disabled:cursor-not-allowed
                flex items-center justify-center'
              style={{
                background: coverOpen
                  ? 'radial-gradient(circle at 35% 35%, #ff4444, #dc2626 40%, #991b1b)'
                  : 'radial-gradient(circle at 35% 35%, #b91c1c, #991b1b 40%, #7f1d1d)',
                boxShadow: coverOpen
                  ? '0 0 16px rgba(239,68,68,0.5), inset 0 -3px 6px rgba(0,0,0,0.3), inset 0 2px 4px rgba(255,255,255,0.15)'
                  : 'inset 0 -3px 6px rgba(0,0,0,0.3), inset 0 2px 4px rgba(255,255,255,0.1)',
              }}
            >
              <span className='text-base font-bold tracking-wider text-red-100/90 text-center leading-tight select-none'>
                {buttonLabel}
              </span>
            </button>

            {/* Label plate */}
            <div className='bg-neutral-700 rounded px-3 py-0.5'>
              <span className='text-[9px] font-mono uppercase tracking-widest text-amber-400/90 select-none'>
                {coverOpen ? plateLabelOpen : plateLabel}
              </span>
            </div>
          </div>
        </div>
      </div>
    </div>
  );
}

export function DangerZone() {
  const [archiveCoverOpen, setArchiveCoverOpen] = useState(false);
  const [lockdownCoverOpen, setLockdownCoverOpen] = useState(false);
  const serverArchived = useChatStore((s) => s.serverArchived);
  const setServerArchived = useChatStore((s) => s.setServerArchived);
  const serverLockedDown = useChatStore((s) => s.serverLockedDown);
  const setServerLockedDown = useChatStore((s) => s.setServerLockedDown);

  return (
    <div className='space-y-10'>
      {/* Lockdown Mode */}
      <div>
        <p className='text-sm font-medium text-foreground mb-1'>
          {serverLockedDown ? 'Server Locked Down' : 'Lockdown Mode'}
        </p>
        <p className='text-xs text-default-400 mb-3'>
          {serverLockedDown
            ? 'The server is currently in lockdown. Only owners and admins can access it.'
            : 'Lockdown mode will immediately kick all non-admin users and prevent them from logging in until lockdown is lifted.'}
        </p>
        {serverLockedDown ? (
          <Button
            color='success'
            variant='flat'
            size='sm'
            onPress={async () => {
              try {
                await api.unlockServer();
                setServerLockedDown(false);
              } catch (e) {
                console.error('Unlock failed:', e);
              }
            }}
          >
            Lift Lockdown
          </Button>
        ) : (
          <EmergencyButton
            coverOpen={lockdownCoverOpen}
            setCoverOpen={setLockdownCoverOpen}
            onPress={async () => {
              if (
                !confirm(
                  'Are you sure you want to lock down the server? All non-admin users will be immediately kicked.',
                )
              )
                return;
              try {
                await api.lockdownServer();
                setServerLockedDown(true);
                setLockdownCoverOpen(false);
              } catch (e) {
                console.error('Lockdown failed:', e);
              }
            }}
            buttonLabel='БЛОК'
            plateLabel='Lockdown'
            plateLabelOpen='Cover open'
          />
        )}
      </div>

      {/* Divider */}
      <div className='border-t border-default-200' />

      {/* Archive Server */}
      <div>
        <p className='text-sm font-medium text-foreground mb-1'>
          {serverArchived ? 'Server Archived' : 'Archive Server'}
        </p>
        <p className='text-xs text-default-400 mb-3'>
          {serverArchived
            ? 'The server is currently archived. Users cannot send messages or create channels.'
            : 'Archiving the server will prevent all users from sending messages or creating channels.'}
        </p>
        {serverArchived ? (
          <Button
            color='success'
            variant='flat'
            size='sm'
            onPress={async () => {
              try {
                await api.unarchiveServer();
                setServerArchived(false);
              } catch (e) {
                console.error('Unarchive failed:', e);
              }
            }}
          >
            Unarchive Server
          </Button>
        ) : (
          <EmergencyButton
            coverOpen={archiveCoverOpen}
            setCoverOpen={setArchiveCoverOpen}
            onPress={async () => {
              if (
                !confirm(
                  'Are you sure you want to archive the server? All messaging will be disabled.',
                )
              )
                return;
              try {
                await api.archiveServer();
                setServerArchived(true);
                setArchiveCoverOpen(false);
              } catch (e) {
                console.error('Archive failed:', e);
              }
            }}
            buttonLabel='АЗ-5'
            plateLabel='Emergency'
            plateLabelOpen='Cover open'
          />
        )}
      </div>
    </div>
  );
}
