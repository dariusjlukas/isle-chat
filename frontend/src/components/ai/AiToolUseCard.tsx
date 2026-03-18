import { useState } from 'react';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import {
  faChevronDown,
  faChevronRight,
  faCheck,
  faXmark,
} from '@fortawesome/free-solid-svg-icons';
import type { AiToolUse } from '../../types';

function formatToolName(name: string): string {
  return name.replace(/_/g, ' ').replace(/\b\w/g, (c) => c.toUpperCase());
}

interface Props {
  toolUse: AiToolUse;
}

export function AiToolUseCard({ toolUse }: Props) {
  const [expanded, setExpanded] = useState(false);

  const isSuccess = toolUse.status === 'success';

  return (
    <div className='my-2 max-w-[85%] sm:max-w-[70%]'>
      <div className='border border-divider rounded-lg overflow-hidden bg-content1'>
        <button
          className='flex items-center gap-2 w-full px-3 py-2 text-left text-sm hover:bg-content2 transition-colors'
          onClick={() => setExpanded(!expanded)}
        >
          <FontAwesomeIcon
            icon={expanded ? faChevronDown : faChevronRight}
            className='text-[10px] text-default-400 flex-shrink-0'
          />
          <span className='font-medium text-default-700 flex-1 truncate'>
            {formatToolName(toolUse.tool_name)}
          </span>
          <FontAwesomeIcon
            icon={isSuccess ? faCheck : faXmark}
            className={`text-xs flex-shrink-0 ${
              isSuccess ? 'text-success' : 'text-danger'
            }`}
          />
        </button>
        {expanded && (
          <div className='border-t border-divider px-3 py-2 space-y-2'>
            <div>
              <p className='text-xs font-semibold text-default-500 mb-1'>
                Arguments
              </p>
              <pre className='text-xs bg-content2 rounded-md p-2 overflow-x-auto whitespace-pre-wrap break-words'>
                {JSON.stringify(toolUse.arguments, null, 2)}
              </pre>
            </div>
            {toolUse.result !== undefined && toolUse.result !== null && (
              <div>
                <p className='text-xs font-semibold text-default-500 mb-1'>
                  Result
                </p>
                <pre className='text-xs bg-content2 rounded-md p-2 overflow-x-auto whitespace-pre-wrap break-words'>
                  {typeof toolUse.result === 'string'
                    ? toolUse.result
                    : JSON.stringify(toolUse.result, null, 2)}
                </pre>
              </div>
            )}
          </div>
        )}
      </div>
    </div>
  );
}
