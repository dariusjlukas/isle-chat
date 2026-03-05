import { useState, useRef, useEffect, useCallback } from 'react';
import { Input, Chip, Tab, Tabs, Button, Spinner, Kbd } from '@heroui/react';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import {
  faMagnifyingGlass,
  faDownload,
  faFile,
  faMessage,
  faArrowUpRightFromSquare,
  faHashtag,
} from '@fortawesome/free-solid-svg-icons';
import * as api from '../../services/api';
import type { User } from '../../types';
import type {
  MessageSearchResult,
  FileSearchResult,
  SpaceSearchResult,
  ChannelSearchResult,
} from '../../services/api';
import { useChatStore } from '../../stores/chatStore';
import { OnlineStatusDot } from '../common/OnlineStatusDot';
import { UserPopoverCard } from '../common/UserPopoverCard';
import { relativeTime } from '../../utils/time';

type SearchTab = 'messages' | 'users' | 'files' | 'channels' | 'spaces';

interface SearchChip {
  id: string;
  type: SearchTab;
  value: string;
}

const TAB_LABELS: Record<SearchTab, string> = {
  messages: 'Text',
  users: 'User',
  files: 'File',
  channels: 'Channel',
  spaces: 'Space',
};

let chipIdCounter = 0;

export function GlobalSearch() {
  const [inputValue, setInputValue] = useState('');
  const [chips, setChips] = useState<SearchChip[]>([]);
  const [isOpen, setIsOpen] = useState(false);
  const [activeTab, setActiveTab] = useState<SearchTab>('messages');
  const [mode, setMode] = useState<'and' | 'or'>('and');
  const [loading, setLoading] = useState(false);

  const [userResults, setUserResults] = useState<User[]>([]);
  const [messageResults, setMessageResults] = useState<MessageSearchResult[]>(
    [],
  );
  const [fileResults, setFileResults] = useState<FileSearchResult[]>([]);
  const [channelResults, setChannelResults] = useState<ChannelSearchResult[]>(
    [],
  );
  const [spaceResults, setSpaceResults] = useState<SpaceSearchResult[]>([]);

  const containerRef = useRef<HTMLDivElement>(null);
  const inputRef = useRef<HTMLInputElement>(null);
  const debounceRef = useRef<ReturnType<typeof setTimeout>>(undefined);

  const currentUser = useChatStore((s) => s.user);
  const setActiveChannel = useChatStore((s) => s.setActiveChannel);
  const setJumpToMessage = useChatStore((s) => s.setJumpToMessage);
  const channels = useChatStore((s) => s.channels);

  const clearAllResults = useCallback(() => {
    setUserResults([]);
    setMessageResults([]);
    setFileResults([]);
    setChannelResults([]);
    setSpaceResults([]);
  }, []);

  const clearSearch = useCallback(() => {
    setInputValue('');
    setChips([]);
    clearAllResults();
    setIsOpen(false);
  }, [clearAllResults]);

  // Close on click outside
  useEffect(() => {
    function handleClickOutside(e: MouseEvent) {
      if (
        containerRef.current &&
        !containerRef.current.contains(e.target as Node)
      ) {
        clearSearch();
      }
    }
    document.addEventListener('mousedown', handleClickOutside);
    return () => document.removeEventListener('mousedown', handleClickOutside);
  }, [clearSearch]);

  // Ref to avoid forward-declaration issues with addChipFromInput
  const addChipRef = useRef<() => void>(() => {});

  // Global keyboard shortcuts when dropdown is open
  useEffect(() => {
    function handleKeyDown(e: KeyboardEvent) {
      if (e.key === 'Escape') clearSearch();
      if (e.key === 'Enter') {
        e.preventDefault();
        addChipRef.current();
      }
    }
    if (isOpen) {
      // Use capture phase so we intercept Enter before HeroUI's Tab handlers stop propagation
      document.addEventListener('keydown', handleKeyDown, true);
      return () => document.removeEventListener('keydown', handleKeyDown, true);
    }
  }, [isOpen, clearSearch]);

  // Search for a single tab (no chips, live preview while typing)
  const doSingleSearch = useCallback(
    (searchQuery: string, tab: SearchTab, searchMode: 'and' | 'or') => {
      if (!searchQuery.trim()) {
        clearAllResults();
        return;
      }

      setLoading(true);

      let promise: Promise<void>;
      switch (tab) {
        case 'users':
          promise = api
            .searchUsers(searchQuery)
            .then((r) => setUserResults(r.results));
          break;
        case 'messages':
          promise = api
            .searchMessages(searchQuery, searchMode)
            .then((r) => setMessageResults(r.results));
          break;
        case 'files':
          promise = api
            .searchFiles(searchQuery)
            .then((r) => setFileResults(r.results));
          break;
        case 'channels':
          promise = api
            .searchChannels(searchQuery)
            .then((r) => setChannelResults(r.results));
          break;
        case 'spaces':
          promise = api
            .searchSpaces(searchQuery)
            .then((r) => setSpaceResults(r.results));
          break;
      }

      promise.catch(() => clearAllResults()).finally(() => setLoading(false));
    },
    [clearAllResults],
  );

  // Search with chips as cross-tab filters. Results depend on activeTab (result_type).
  const doCompositeSearch = useCallback(
    (filterChips: SearchChip[], tab: SearchTab, searchMode: 'and' | 'or') => {
      if (filterChips.length === 0) {
        clearAllResults();
        return;
      }

      setLoading(true);
      const filters = filterChips.map((c) => ({
        type: c.type,
        value: c.value,
      }));

      let promise: Promise<void>;
      switch (tab) {
        case 'messages':
          promise = api
            .searchComposite<MessageSearchResult>(
              filters,
              'messages',
              searchMode,
            )
            .then((r) => setMessageResults(r.results));
          break;
        case 'users':
          promise = api
            .searchComposite<User>(filters, 'users', searchMode)
            .then((r) => setUserResults(r.results as User[]));
          break;
        case 'files':
          promise = api
            .searchComposite<FileSearchResult>(filters, 'files', searchMode)
            .then((r) => setFileResults(r.results));
          break;
        case 'channels':
          promise = api
            .searchComposite<ChannelSearchResult>(
              filters,
              'channels',
              searchMode,
            )
            .then((r) => setChannelResults(r.results));
          break;
        case 'spaces':
          promise = api
            .searchComposite<SpaceSearchResult>(filters, 'spaces', searchMode)
            .then((r) => setSpaceResults(r.results));
          break;
      }

      promise!.catch(() => clearAllResults()).finally(() => setLoading(false));
    },
    [clearAllResults],
  );

  const scheduleSearch = useCallback(
    (searchQuery: string, tab: SearchTab, searchMode: 'and' | 'or') => {
      clearTimeout(debounceRef.current);
      debounceRef.current = setTimeout(
        () => doSingleSearch(searchQuery, tab, searchMode),
        300,
      );
    },
    [doSingleSearch],
  );

  const handleInputChange = (value: string) => {
    setInputValue(value);
    if (!isOpen) setIsOpen(true);
    // Only live-search when no chips exist
    if (chips.length === 0) {
      scheduleSearch(value, activeTab, mode);
    }
  };

  const addChipFromInput = useCallback(() => {
    if (!inputValue.trim()) return;
    const newChip: SearchChip = {
      id: String(++chipIdCounter),
      type: activeTab,
      value: inputValue.trim(),
    };
    const newChips = [...chips, newChip];
    setChips(newChips);
    setInputValue('');
    doCompositeSearch(newChips, activeTab, mode);
    inputRef.current?.focus();
  }, [inputValue, activeTab, chips, doCompositeSearch, mode]);

  // Keep ref in sync
  addChipRef.current = addChipFromInput;

  const TABS_ORDER: SearchTab[] = [
    'messages',
    'users',
    'files',
    'channels',
    'spaces',
  ];

  const handleKeyDown = (e: React.KeyboardEvent) => {
    if (e.key === 'Tab' && isOpen) {
      e.preventDefault();
      const dir = e.shiftKey ? -1 : 1;
      const idx = TABS_ORDER.indexOf(activeTab);
      const next =
        TABS_ORDER[(idx + dir + TABS_ORDER.length) % TABS_ORDER.length];
      handleTabChange(next);
      return;
    }
    if (e.key === 'Enter' && inputValue.trim()) {
      e.preventDefault();
      addChipFromInput();
    } else if (e.key === 'Backspace' && !inputValue && chips.length > 0) {
      const newChips = chips.slice(0, -1);
      setChips(newChips);
      if (newChips.length > 0) {
        doCompositeSearch(newChips, activeTab, mode);
      } else {
        clearAllResults();
      }
    }
  };

  const removeChip = (chipId: string) => {
    const newChips = chips.filter((c) => c.id !== chipId);
    setChips(newChips);
    if (newChips.length > 0) {
      doCompositeSearch(newChips, activeTab, mode);
    } else {
      clearAllResults();
    }
  };

  const handleTabChange = (tab: SearchTab) => {
    setActiveTab(tab);
    if (chips.length > 0) {
      // Re-fire composite search for the new tab's result type
      doCompositeSearch(chips, tab, mode);
    } else if (inputValue.trim()) {
      doSingleSearch(inputValue, tab, mode);
    }
  };

  const handleModeToggle = (isOr: boolean) => {
    const newMode = isOr ? 'or' : 'and';
    setMode(newMode);
    if (chips.length > 0) {
      doCompositeSearch(chips, activeTab, newMode);
    } else if (inputValue.trim()) {
      doSingleSearch(inputValue, activeTab, newMode);
    }
  };

  const handleJumpToMessage = (channelId: string, messageId: string) => {
    const channel = channels.find((c) => c.id === channelId);
    if (channel?.space_id) {
      useChatStore
        .getState()
        .setActiveView({ type: 'space', spaceId: channel.space_id });
    } else if (channel?.is_direct) {
      useChatStore.getState().setActiveView({ type: 'messages' });
    }
    setActiveChannel(channelId);
    setJumpToMessage(channelId, messageId);
    clearSearch();
  };

  const handleNavigateToChannel = (channelId: string, spaceId?: string) => {
    if (spaceId) {
      useChatStore.getState().setActiveView({ type: 'space', spaceId });
    }
    setActiveChannel(channelId);
    clearSearch();
  };

  const handleNavigateToSpace = (spaceId: string) => {
    useChatStore.getState().setActiveView({ type: 'space', spaceId });
    clearSearch();
  };

  const handleStartDM = async (userId: string) => {
    try {
      const dm = await api.createDM(userId);
      useChatStore.getState().setActiveView({ type: 'messages' });
      setActiveChannel(dm.id);
      clearSearch();
    } catch (e) {
      console.error('Failed to create DM:', e);
    }
  };

  const handleDownload = async (fileId: string, fileName: string) => {
    try {
      await api.downloadFile(fileId, fileName);
    } catch (e) {
      console.error('Download failed:', e);
    }
  };

  const hasContent = chips.length > 0 || inputValue.trim();

  const renderResults = () => {
    if (loading) {
      return (
        <div className="flex items-center justify-center py-8">
          <Spinner size="sm" />
        </div>
      );
    }

    // Always render results based on activeTab
    switch (activeTab) {
      case 'users': {
        const filtered = userResults.filter((u) => u.id !== currentUser?.id);
        if (filtered.length === 0)
          return (
            <p className="text-sm text-default-400 text-center py-8">
              No results found
            </p>
          );
        return filtered.map((u) => (
          <UserResultItem
            key={u.id}
            user={u}
            onStartDM={() => handleStartDM(u.id)}
          />
        ));
      }
      case 'messages':
        if (messageResults.length === 0)
          return (
            <p className="text-sm text-default-400 text-center py-8">
              No results found
            </p>
          );
        return messageResults.map((m) => (
          <MessageResultItem
            key={m.id}
            result={m}
            onJump={() => handleJumpToMessage(m.channel_id, m.id)}
          />
        ));
      case 'files':
        if (fileResults.length === 0)
          return (
            <p className="text-sm text-default-400 text-center py-8">
              No results found
            </p>
          );
        return fileResults.map((f) => (
          <FileResultItem
            key={f.file_id}
            result={f}
            onDownload={() => handleDownload(f.file_id, f.file_name)}
            onJump={() => handleJumpToMessage(f.channel_id, f.message_id)}
          />
        ));
      case 'channels':
        if (channelResults.length === 0)
          return (
            <p className="text-sm text-default-400 text-center py-8">
              No results found
            </p>
          );
        return channelResults.map((c) => (
          <ChannelResultItem
            key={c.id}
            result={c}
            onNavigate={() =>
              handleNavigateToChannel(c.id, c.space_id || undefined)
            }
          />
        ));
      case 'spaces':
        if (spaceResults.length === 0)
          return (
            <p className="text-sm text-default-400 text-center py-8">
              No results found
            </p>
          );
        return spaceResults.map((s) => (
          <SpaceResultItem
            key={s.id}
            result={s}
            onNavigate={() => handleNavigateToSpace(s.id)}
          />
        ));
    }
  };

  return (
    <div ref={containerRef} className="relative w-full max-w-xl mx-auto">
      <Input
        ref={inputRef}
        placeholder={
          chips.length > 0 ? `Add ${activeTab} filter...` : 'Search...'
        }
        variant="bordered"
        size="sm"
        value={inputValue}
        onValueChange={handleInputChange}
        onKeyDown={handleKeyDown}
        onFocus={() => {
          if (hasContent) setIsOpen(true);
        }}
        startContent={
          <FontAwesomeIcon
            icon={faMagnifyingGlass}
            className="text-default-400 text-sm"
          />
        }
        classNames={{
          inputWrapper: 'h-8',
          input: 'text-sm',
        }}
        endContent={
          <div className="flex items-center gap-1">
            {inputValue.trim() && (
              <Button
                size="sm"
                variant="flat"
                className="h-5 min-w-0 px-1.5 gap-1 text-[10px]"
                onPress={addChipFromInput}
              >
                Add Filter{' '}
                <Kbd
                  className="text-[10px] px-1 py-0 min-h-0 h-3.5 bg-default-100"
                  keys={['enter']}
                />
              </Button>
            )}
            {(inputValue || chips.length > 0) && (
              <button
                className="text-default-400 hover:text-default-600 text-sm px-0.5"
                onClick={() => {
                  setInputValue('');
                  setChips([]);
                  clearAllResults();
                  setIsOpen(false);
                }}
              >
                &times;
              </button>
            )}
          </div>
        }
      />

      {isOpen && hasContent && (
        <div className="absolute top-full left-0 right-0 mt-1 bg-content1 border border-default-200 rounded-xl shadow-lg z-50 overflow-hidden">
          {/* Chips area */}
          {chips.length > 0 && (
            <div className="flex flex-wrap items-center gap-1 px-3 pt-2">
              {chips.map((chip, i) => (
                <div key={chip.id} className="flex items-center gap-1">
                  {i > 0 && (
                    <span className="text-[10px] font-semibold text-default-400 px-0.5">
                      {mode.toUpperCase()}
                    </span>
                  )}
                  <Chip
                    size="sm"
                    variant="flat"
                    onClose={() => removeChip(chip.id)}
                    classNames={{ base: 'h-6' }}
                  >
                    <span className="text-[10px] font-semibold text-primary mr-0.5">
                      {TAB_LABELS[chip.type]}:
                    </span>
                    {chip.value}
                  </Chip>
                </div>
              ))}
            </div>
          )}

          {/* Tabs + AND/OR toggle */}
          <div className="flex items-center justify-between px-3 pt-2 pb-1">
            <Tabs
              size="sm"
              variant="underlined"
              selectedKey={activeTab}
              onSelectionChange={(key) => handleTabChange(key as SearchTab)}
              classNames={{ tabList: 'gap-2' }}
            >
              <Tab key="messages" title="Messages" />
              <Tab key="users" title="Users" />
              <Tab key="files" title="Files" />
              <Tab key="channels" title="Channels" />
              <Tab key="spaces" title="Spaces" />
            </Tabs>
            <div className="flex items-center gap-0 flex-shrink-0 border border-default-200 rounded-lg overflow-hidden">
              <button
                className={`px-2 py-0.5 text-[11px] font-medium transition-colors ${mode === 'and' ? 'bg-primary text-primary-foreground' : 'text-default-500 hover:bg-default-100'}`}
                onClick={() => handleModeToggle(false)}
              >
                AND
              </button>
              <button
                className={`px-2 py-0.5 text-[11px] font-medium transition-colors ${mode === 'or' ? 'bg-primary text-primary-foreground' : 'text-default-500 hover:bg-default-100'}`}
                onClick={() => handleModeToggle(true)}
              >
                OR
              </button>
            </div>
          </div>

          {/* Results */}
          <div className="max-h-80 overflow-y-auto px-1 pb-1">
            {renderResults()}
          </div>
        </div>
      )}
    </div>
  );
}

function UserResultItem({
  user,
  onStartDM,
}: {
  user: User;
  onStartDM: () => void;
}) {
  return (
    <div className="flex items-center gap-2 p-2 mx-1 rounded-lg hover:bg-content2/50 transition-colors">
      <OnlineStatusDot isOnline={user.is_online} lastSeen={user.last_seen} />
      <div className="flex-1 min-w-0">
        <UserPopoverCard user={user}>
          <span className="text-sm font-medium cursor-pointer hover:underline truncate">
            {user.display_name}
          </span>
        </UserPopoverCard>
        <span className="text-xs text-default-400 ml-1.5">
          @{user.username}
        </span>
      </div>
      <Button
        size="sm"
        variant="flat"
        onPress={onStartDM}
        startContent={<FontAwesomeIcon icon={faMessage} className="text-xs" />}
      >
        Message
      </Button>
    </div>
  );
}

function MessageResultItem({
  result,
  onJump,
}: {
  result: MessageSearchResult;
  onJump: () => void;
}) {
  const breadcrumb = result.is_direct
    ? 'DM'
    : result.space_name
      ? `${result.space_name} / #${result.channel_name}`
      : `#${result.channel_name}`;

  return (
    <div
      className="p-2 mx-1 rounded-lg hover:bg-content2/50 transition-colors cursor-pointer"
      onClick={onJump}
    >
      <div className="flex items-center gap-2 mb-0.5">
        <Chip size="sm" variant="flat" className="h-5 text-[10px]">
          {breadcrumb}
        </Chip>
        <span className="text-xs text-default-400">@{result.username}</span>
        <span className="text-xs text-default-300 ml-auto flex-shrink-0">
          {relativeTime(result.created_at)}
        </span>
      </div>
      <p className="text-sm text-foreground line-clamp-2">{result.content}</p>
    </div>
  );
}

function FileResultItem({
  result,
  onDownload,
  onJump,
}: {
  result: FileSearchResult;
  onDownload: () => void;
  onJump: () => void;
}) {
  const formatSize = (bytes: number) => {
    if (bytes < 1024) return bytes + ' B';
    if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + ' KB';
    return (bytes / (1024 * 1024)).toFixed(1) + ' MB';
  };

  return (
    <div className="flex items-center gap-2 p-2 mx-1 rounded-lg hover:bg-content2/50 transition-colors">
      <FontAwesomeIcon
        icon={faFile}
        className="text-default-400 flex-shrink-0"
      />
      <div className="flex-1 min-w-0">
        <p className="text-sm font-medium truncate">{result.file_name}</p>
        <p className="text-xs text-default-400">
          {formatSize(result.file_size)} &middot; @{result.username} &middot;{' '}
          {result.channel_name}
        </p>
      </div>
      <Button
        isIconOnly
        size="sm"
        variant="light"
        onPress={onDownload}
        title="Download"
      >
        <FontAwesomeIcon icon={faDownload} className="text-xs" />
      </Button>
      <Button
        isIconOnly
        size="sm"
        variant="light"
        onPress={onJump}
        title="Jump to message"
      >
        <FontAwesomeIcon icon={faArrowUpRightFromSquare} className="text-xs" />
      </Button>
    </div>
  );
}

function ChannelResultItem({
  result,
  onNavigate,
}: {
  result: ChannelSearchResult;
  onNavigate: () => void;
}) {
  return (
    <div
      className="flex items-center gap-2 p-2 mx-1 rounded-lg hover:bg-content2/50 transition-colors cursor-pointer"
      onClick={onNavigate}
    >
      <FontAwesomeIcon
        icon={faHashtag}
        className="text-default-400 flex-shrink-0 text-sm"
      />
      <div className="flex-1 min-w-0">
        <p className="text-sm font-medium truncate">
          {result.space_name
            ? `${result.space_name} / #${result.name}`
            : `#${result.name}`}
        </p>
        {result.description && (
          <p className="text-xs text-default-400 truncate">
            {result.description}
          </p>
        )}
      </div>
    </div>
  );
}

function SpaceResultItem({
  result,
  onNavigate,
}: {
  result: SpaceSearchResult;
  onNavigate: () => void;
}) {
  return (
    <div
      className="flex items-center gap-2 p-2 mx-1 rounded-lg hover:bg-content2/50 transition-colors cursor-pointer"
      onClick={onNavigate}
    >
      {result.icon ? (
        <span className="text-sm flex-shrink-0">{result.icon}</span>
      ) : (
        <span className="w-5 h-5 rounded bg-primary/20 flex items-center justify-center text-[10px] font-bold text-primary flex-shrink-0">
          {result.name[0]?.toUpperCase()}
        </span>
      )}
      <div className="flex-1 min-w-0">
        <p className="text-sm font-medium truncate">{result.name}</p>
        {result.description && (
          <p className="text-xs text-default-400 truncate">
            {result.description}
          </p>
        )}
      </div>
    </div>
  );
}
