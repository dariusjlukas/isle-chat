import { useState, useEffect, lazy, Suspense } from 'react';
import { Modal, ModalContent, ModalBody } from '@heroui/react';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import { faWifi, faMicrochip, faCube } from '@fortawesome/free-solid-svg-icons';
import {
  useConnectionState,
  useHasConnected,
} from '../../hooks/useConnectionState';
import { PCBRouter } from './PCBRouter';

// Lazy-load the three.js-backed Rubik's cube — avoids bundling three.js
// into the main chunk just so the disconnect modal can offer a minigame.
const RubiksCube = lazy(() =>
  import('./RubiksCube').then((m) => ({ default: m.RubiksCube })),
);

type GameChoice = null | 'pcb' | 'rubiks';

const RECONNECT_SECONDS = 5;

export function ConnectionLostModal() {
  const connectionState = useConnectionState();
  const hasConnected = useHasConnected();
  const [countdown, setCountdown] = useState(RECONNECT_SECONDS);
  const [activeGame, setActiveGame] = useState<GameChoice>(null);

  // Only show after we've been connected at least once (skip initial connect after login)
  const showModal = hasConnected && connectionState !== 'connected';

  // Reset game when modal closes — derived from showModal, not set in an effect
  const effectiveGame: GameChoice = activeGame && showModal ? activeGame : null;

  useEffect(() => {
    if (!showModal) return;
    const start = Date.now();
    const timer = setInterval(() => {
      const elapsed = Math.floor((Date.now() - start) / 1000);
      const remaining = RECONNECT_SECONDS - (elapsed % (RECONNECT_SECONDS + 1));
      setCountdown(remaining);
    }, 250);
    return () => clearInterval(timer);
  }, [showModal]);

  return (
    <Modal
      isOpen={showModal}
      isDismissable={false}
      hideCloseButton
      backdrop='blur'
      size={effectiveGame === 'rubiks' ? 'xl' : effectiveGame ? 'lg' : 'sm'}
      classNames={{ wrapper: '!items-center !pt-0' }}
    >
      <ModalContent>
        <ModalBody className='py-10 text-center'>
          <div className='flex justify-center mb-5'>
            <div
              className='relative'
              style={{ animation: 'float 2s ease-in-out infinite' }}
            >
              <FontAwesomeIcon
                icon={faWifi}
                className='text-5xl text-danger'
                style={{ animation: 'pulse-glow 1.5s ease-in-out infinite' }}
              />
              <div
                className='absolute inset-[-6px] flex items-center justify-center'
                style={{ transform: 'rotate(-45deg)' }}
              >
                <div className='w-full h-[3px] rounded-full bg-danger' />
              </div>
            </div>
          </div>
          <h3 className='text-lg font-semibold text-foreground mb-2'>
            Connection Lost
          </h3>
          <p className='text-default-500 text-sm mb-4'>
            Hang tight! Reconnecting in{' '}
            <span className='font-mono font-semibold text-foreground'>
              {countdown}s
            </span>
            ...
          </p>
          <div className='flex justify-center gap-1.5'>
            {[0, 1, 2].map((i) => (
              <span
                key={i}
                className='w-2 h-2 rounded-full bg-primary'
                style={{
                  animation: 'bounce-dot 1.2s ease-in-out infinite',
                  animationDelay: `${i * 0.2}s`,
                }}
              />
            ))}
          </div>

          {effectiveGame ? (
            <div className='mt-4 pt-4 border-t border-default-200'>
              {effectiveGame === 'pcb' && <PCBRouter />}
              {effectiveGame === 'rubiks' && (
                <Suspense fallback={null}>
                  <RubiksCube />
                </Suspense>
              )}
              <button
                onClick={() => setActiveGame(null)}
                className='mt-3 text-xs text-default-400 hover:text-default-600 transition-colors cursor-pointer'
              >
                ← Back to game selection
              </button>
            </div>
          ) : (
            <div className='mt-4 flex justify-center gap-3'>
              <button
                onClick={() => setActiveGame('pcb')}
                className='flex flex-col items-center gap-1.5 px-4 py-2.5 rounded-lg bg-content2 hover:bg-content3 transition-colors cursor-pointer'
              >
                <FontAwesomeIcon
                  icon={faMicrochip}
                  className='text-lg text-default-500'
                />
                <span className='text-xs font-medium text-foreground'>
                  Route PCB
                </span>
              </button>
              <button
                onClick={() => setActiveGame('rubiks')}
                className='flex flex-col items-center gap-1.5 px-4 py-2.5 rounded-lg bg-content2 hover:bg-content3 transition-colors cursor-pointer'
              >
                <FontAwesomeIcon
                  icon={faCube}
                  className='text-lg text-default-500'
                />
                <span className='text-xs font-medium text-foreground'>
                  Rubik's Cube
                </span>
              </button>
            </div>
          )}

          <style>{`
            @keyframes float {
              0%, 100% { transform: translateY(0) rotate(0deg); }
              25% { transform: translateY(-8px) rotate(-3deg); }
              75% { transform: translateY(-4px) rotate(3deg); }
            }
            @keyframes pulse-glow {
              0%, 100% { opacity: 0.6; }
              50% { opacity: 1; }
            }
            @keyframes bounce-dot {
              0%, 80%, 100% { transform: translateY(0); opacity: 0.4; }
              40% { transform: translateY(-6px); opacity: 1; }
            }
          `}</style>
        </ModalBody>
      </ModalContent>
    </Modal>
  );
}
