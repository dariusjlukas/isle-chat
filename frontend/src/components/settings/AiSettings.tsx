import { useState, useEffect } from 'react';
import { Switch, Divider } from '@heroui/react';
import { useAiStore } from '../../stores/aiStore';
import * as api from '../../services/api';

interface ToolGroup {
  label: string;
  readKey: string;
  readDesc: string;
  writeKey?: string;
  writeDesc?: string;
}

const toolGroups: ToolGroup[] = [
  {
    label: 'Search & Discovery',
    readKey: 'search',
    readDesc: 'Look up spaces, channels, members, and search messages',
  },
  {
    label: 'Messaging',
    readKey: 'messaging_read',
    readDesc: 'View channels and their members',
    writeKey: 'messaging_write',
    writeDesc: 'Send messages to channels on your behalf',
  },
  {
    label: 'Tasks',
    readKey: 'tasks_read',
    readDesc: 'View task boards and tasks',
    writeKey: 'tasks_write',
    writeDesc: 'Create and update tasks',
  },
  {
    label: 'Calendar',
    readKey: 'calendar_read',
    readDesc: 'View calendar events',
    writeKey: 'calendar_write',
    writeDesc: 'Create calendar events',
  },
  {
    label: 'Wiki',
    readKey: 'wiki_read',
    readDesc: 'View wiki pages and their content',
    writeKey: 'wiki_write',
    writeDesc: 'Create and edit wiki pages',
  },
  {
    label: 'Files',
    readKey: 'files_read',
    readDesc: 'Browse files and folders in spaces',
  },
];

export function AiSettings() {
  const agentEnabled = useAiStore((s) => s.agentEnabled);
  const enabledToolCategories = useAiStore((s) => s.enabledToolCategories);
  const setAgentEnabled = useAiStore((s) => s.setAgentEnabled);
  const setToolCategory = useAiStore((s) => s.setToolCategory);
  const setPreferences = useAiStore((s) => s.setPreferences);
  const [loaded, setLoaded] = useState(false);

  useEffect(() => {
    api.getUserSettings().then((settings) => {
      setPreferences(settings);
      setLoaded(true);
    });
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  const handleToggleAgent = async (enabled: boolean) => {
    setAgentEnabled(enabled);
    await api.updateUserSettings({
      agent_enabled: enabled ? 'true' : 'false',
    });
  };

  const handleToggleTool = async (key: string, enabled: boolean) => {
    setToolCategory(key, enabled);
    await api.updateUserSettings({
      [`agent_tools_${key}`]: enabled ? 'true' : 'false',
    });
  };

  if (!loaded) return null;

  return (
    <div className='space-y-6'>
      <div>
        <h3 className='text-lg font-semibold mb-2'>AI Assistant</h3>
        <p className='text-sm text-default-500 mb-4'>
          Configure the AI assistant that can help you interact with the
          platform.
        </p>
      </div>

      <div className='flex items-center justify-between'>
        <div>
          <p className='font-medium'>Enable AI Assistant</p>
          <p className='text-sm text-default-500'>
            Allow the AI assistant to help you with tasks
          </p>
        </div>
        <Switch isSelected={agentEnabled} onValueChange={handleToggleAgent} />
      </div>

      <Divider />

      <div>
        <h4 className='font-medium mb-1'>Tool Permissions</h4>
        <p className='text-sm text-default-500 mb-4'>
          Control what the AI assistant can see and do on your behalf.
        </p>
        <div className='space-y-5'>
          {toolGroups.map((group) => (
            <div key={group.readKey}>
              <p className='text-sm font-medium mb-2'>{group.label}</p>
              <div className='space-y-2 pl-3'>
                <div className='flex items-center justify-between py-0.5'>
                  <div>
                    <p className='text-xs text-default-500'>Read</p>
                    <p className='text-xs text-default-400'>{group.readDesc}</p>
                  </div>
                  <Switch
                    size='sm'
                    isSelected={enabledToolCategories[group.readKey] ?? true}
                    onValueChange={(v) => handleToggleTool(group.readKey, v)}
                    isDisabled={!agentEnabled}
                  />
                </div>
                {group.writeKey && (
                  <div className='flex items-center justify-between py-0.5'>
                    <div>
                      <p className='text-xs text-default-500'>Write</p>
                      <p className='text-xs text-default-400'>
                        {group.writeDesc}
                      </p>
                    </div>
                    <Switch
                      size='sm'
                      isSelected={enabledToolCategories[group.writeKey] ?? true}
                      onValueChange={(v) =>
                        handleToggleTool(group.writeKey!, v)
                      }
                      isDisabled={!agentEnabled}
                    />
                  </div>
                )}
              </div>
            </div>
          ))}
        </div>
      </div>
    </div>
  );
}
