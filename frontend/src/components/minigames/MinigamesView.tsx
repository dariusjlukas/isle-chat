import { useState } from 'react';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import {
  faMicrochip,
  faCube,
  faPuzzlePiece,
} from '@fortawesome/free-solid-svg-icons';
import { PCBRouter } from '../common/PCBRouter';
import { RubiksCube } from '../common/RubiksCube';

type GameChoice = null | 'pcb' | 'rubiks';

export function MinigamesView(props: { spaceId: string }) {
  void props;
  const [activeGame, setActiveGame] = useState<GameChoice>(null);

  return (
    <div className='flex-1 flex flex-col h-full overflow-hidden'>
      <div className='border-b border-default-200 px-4 py-3 flex items-center gap-2 flex-shrink-0'>
        <FontAwesomeIcon icon={faPuzzlePiece} className='text-default-500' />
        <h2 className='text-sm font-semibold text-foreground'>Minigames</h2>
      </div>

      <div className='flex-1 overflow-auto p-4'>
        {activeGame ? (
          <div className='flex flex-col items-center justify-center h-full w-full'>
            <div className='w-full max-w-2xl mx-auto'>
              {activeGame === 'pcb' && <PCBRouter large />}
              {activeGame === 'rubiks' && <RubiksCube large />}
            </div>
            <div className='flex justify-center mt-4'>
              <button
                onClick={() => setActiveGame(null)}
                className='text-sm text-default-400 hover:text-default-600 transition-colors cursor-pointer'
              >
                ← Back to game selection
              </button>
            </div>
          </div>
        ) : (
          <div className='flex flex-col items-center justify-center h-full gap-6'>
            <p className='text-default-400 text-sm'>Choose a game to play</p>
            <div className='flex gap-4'>
              <button
                onClick={() => setActiveGame('pcb')}
                className='flex flex-col items-center gap-2 px-8 py-6 rounded-xl bg-content2 hover:bg-content3 transition-colors cursor-pointer'
              >
                <FontAwesomeIcon
                  icon={faMicrochip}
                  className='text-3xl text-default-500'
                />
                <span className='text-sm font-medium text-foreground'>
                  Route PCB
                </span>
                <span className='text-xs text-default-400'>
                  Route circuit board traces
                </span>
              </button>
              <button
                onClick={() => setActiveGame('rubiks')}
                className='flex flex-col items-center gap-2 px-8 py-6 rounded-xl bg-content2 hover:bg-content3 transition-colors cursor-pointer'
              >
                <FontAwesomeIcon
                  icon={faCube}
                  className='text-3xl text-default-500'
                />
                <span className='text-sm font-medium text-foreground'>
                  Rubik's Cube
                </span>
                <span className='text-xs text-default-400'>
                  Solve the classic puzzle
                </span>
              </button>
            </div>
          </div>
        )}
      </div>
    </div>
  );
}
