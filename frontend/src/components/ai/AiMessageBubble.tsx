import { useMemo } from 'react';
import { Tooltip } from '@heroui/react';
import type { AiMessage } from '../../types';

function escapeHtml(text: string): string {
  return text
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;')
    .replace(/'/g, '&#039;');
}

function renderMarkdown(text: string): string {
  // Extract code blocks first to protect them from other transformations
  const codeBlocks: string[] = [];
  let processed = text.replace(
    /```(\w*)\n?([\s\S]*?)```/g,
    (_match, lang: string, code: string) => {
      const escaped = escapeHtml(code.replace(/\n$/, ''));
      const langClass = lang ? ` class="language-${escapeHtml(lang)}"` : '';
      codeBlocks.push(
        `<pre class="bg-content2 rounded-md p-3 my-2 overflow-x-auto text-xs"><code${langClass}>${escaped}</code></pre>`,
      );
      return `__CODEBLOCK_${codeBlocks.length - 1}__`;
    },
  );

  // Escape HTML in remaining text (not code blocks)
  processed = escapeHtml(processed);

  // Inline code
  processed = processed.replace(
    /`([^`]+)`/g,
    '<code class="bg-content2 px-1.5 py-0.5 rounded text-xs">$1</code>',
  );

  // Bold
  processed = processed.replace(/\*\*(.+?)\*\*/g, '<strong>$1</strong>');

  // Italic
  processed = processed.replace(/\*(.+?)\*/g, '<em>$1</em>');

  // Newlines to <br> (outside code blocks)
  processed = processed.replace(/\n/g, '<br>');

  // Restore code blocks
  processed = processed.replace(
    /__CODEBLOCK_(\d+)__/g,
    (_match, idx: string) => codeBlocks[parseInt(idx)],
  );

  return processed;
}

interface Props {
  message: AiMessage;
  isStreaming?: boolean;
}

export function AiMessageBubble({ message, isStreaming }: Props) {
  const isUser = message.role === 'user';

  const renderedContent = useMemo(
    () => renderMarkdown(message.content || ''),
    [message.content],
  );

  const time = new Date(message.created_at).toLocaleTimeString([], {
    hour: '2-digit',
    minute: '2-digit',
  });

  // Don't render tool messages (handled by AiToolUseCard)
  if (message.role === 'tool') return null;

  return (
    <div className={`flex ${isUser ? 'justify-end' : 'justify-start'} mb-3`}>
      <Tooltip
        content={<span className='text-xs'>{time}</span>}
        placement={isUser ? 'left' : 'right'}
        delay={400}
      >
        <div
          className={`max-w-[85%] sm:max-w-[70%] rounded-2xl px-4 py-2 text-foreground ${
            isUser
              ? 'bg-primary text-primary-foreground rounded-br-md'
              : 'bg-content2 rounded-bl-md'
          }`}
        >
          {isUser ? (
            <p className='text-sm whitespace-pre-wrap break-words'>
              {message.content}
            </p>
          ) : (
            <div
              className='text-sm break-words prose prose-sm max-w-none dark:prose-invert prose-pre:p-0 prose-pre:bg-transparent'
              dangerouslySetInnerHTML={{ __html: renderedContent }}
            />
          )}
          {isStreaming && (
            <span className='inline-block w-2 h-4 bg-foreground/60 animate-pulse ml-0.5 align-text-bottom' />
          )}
        </div>
      </Tooltip>
    </div>
  );
}
