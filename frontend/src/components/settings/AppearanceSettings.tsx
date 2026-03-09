import { Select, SelectItem } from '@heroui/react';
import {
  useTheme,
  COLOR_THEMES,
  type ColorTheme,
  type ModeSetting,
} from '../../hooks/useTheme';

export function AppearanceSettings() {
  const { colorTheme, modeSetting, setColorTheme, setModeSetting } = useTheme();

  return (
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
  );
}
