import { Select, SelectItem } from '@heroui/react';
import {
  useTheme,
  COLOR_THEMES,
  UI_SCALES,
  type ColorTheme,
  type ModeSetting,
  type UIScale,
} from '../../hooks/useTheme';

export function AppearanceSettings() {
  const {
    colorTheme,
    modeSetting,
    uiScale,
    setColorTheme,
    setModeSetting,
    setUIScale,
  } = useTheme();

  return (
    <div className='flex flex-col gap-3'>
      <div className='flex flex-col sm:flex-row gap-3'>
        <Select
          label='Color Theme'
          variant='bordered'
          selectedKeys={[colorTheme]}
          onChange={(e) => {
            if (e.target.value) setColorTheme(e.target.value as ColorTheme);
          }}
          className='flex-1'
        >
          {COLOR_THEMES.map(({ key, label }) => (
            <SelectItem key={key}>{label}</SelectItem>
          ))}
        </Select>
        <Select
          label='Mode'
          variant='bordered'
          selectedKeys={[modeSetting]}
          onChange={(e) => {
            if (e.target.value) setModeSetting(e.target.value as ModeSetting);
          }}
          className='flex-1'
        >
          <SelectItem key='auto'>Auto</SelectItem>
          <SelectItem key='light'>Light</SelectItem>
          <SelectItem key='dark'>Dark</SelectItem>
        </Select>
      </div>
      <Select
        label='UI Scale'
        description='Adjust the size of text and UI elements'
        variant='bordered'
        selectedKeys={[uiScale]}
        onChange={(e) => {
          if (e.target.value) setUIScale(e.target.value as UIScale);
        }}
        className='sm:max-w-[calc(50%-0.375rem)]'
      >
        {UI_SCALES.map(({ key, label }) => (
          <SelectItem key={key}>{label}</SelectItem>
        ))}
      </Select>
    </div>
  );
}
