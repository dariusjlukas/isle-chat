import { useState, useEffect } from 'react';
import { Modal, ModalContent, ModalBody } from '@heroui/react';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import { faWifi } from '@fortawesome/free-solid-svg-icons';
import {
  useConnectionState,
  useHasConnected,
} from '../../hooks/useConnectionState';

const RECONNECT_SECONDS = 5;

export function ConnectionLostModal() {
  const connectionState = useConnectionState();
  const hasConnected = useHasConnected();
  const [countdown, setCountdown] = useState(RECONNECT_SECONDS);

  // Only show after we've been connected at least once (skip initial connect after login)
  const showModal = hasConnected && connectionState !== 'connected';

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
      backdrop="blur"
      size="sm"
    >
      <ModalContent>
        <ModalBody className="py-10 text-center">
          <div className="flex justify-center mb-5">
            <div
              className="relative"
              style={{ animation: 'float 2s ease-in-out infinite' }}
            >
              <FontAwesomeIcon
                icon={faWifi}
                className="text-5xl text-danger"
                style={{ animation: 'pulse-glow 1.5s ease-in-out infinite' }}
              />
              <div
                className="absolute inset-[-6px] flex items-center justify-center"
                style={{ transform: 'rotate(-45deg)' }}
              >
                <div className="w-full h-[3px] rounded-full bg-danger" />
              </div>
            </div>
          </div>
          <h3 className="text-lg font-semibold text-foreground mb-2">
            Connection Lost
          </h3>
          <p className="text-default-500 text-sm mb-4">
            Hang tight! Reconnecting in{' '}
            <span className="font-mono font-semibold text-foreground">
              {countdown}s
            </span>
            ...
          </p>
          <div className="flex justify-center gap-1.5">
            {[0, 1, 2].map((i) => (
              <span
                key={i}
                className="w-2 h-2 rounded-full bg-primary"
                style={{
                  animation: 'bounce-dot 1.2s ease-in-out infinite',
                  animationDelay: `${i * 0.2}s`,
                }}
              />
            ))}
          </div>
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
