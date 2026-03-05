import { useState, useCallback } from 'react';

export type ColorTheme =
  | 'modern'
  | 'elegant'
  | 'coffee'
  | 'emerald'
  | 'classic';
export type ModeSetting = 'auto' | 'light' | 'dark';
export type Mode = 'light' | 'dark';

export const COLOR_THEMES: { key: ColorTheme; label: string }[] = [
  { key: 'modern', label: 'Modern' },
  { key: 'elegant', label: 'Elegant' },
  { key: 'coffee', label: 'Coffee' },
  { key: 'emerald', label: 'Emerald' },
  { key: 'classic', label: 'Classic' },
];

const ALL_THEME_CLASSES = COLOR_THEMES.flatMap(({ key }) => [
  `${key}-light`,
  `${key}-dark`,
]);

// Migrate old single 'theme' key to new 'mode' key
if (!localStorage.getItem('mode') && localStorage.getItem('theme')) {
  localStorage.setItem('mode', localStorage.getItem('theme')!);
  localStorage.removeItem('theme');
}

function getStoredColorTheme(): ColorTheme {
  return (localStorage.getItem('colorTheme') as ColorTheme) || 'modern';
}

function getStoredModeSetting(): ModeSetting {
  return (localStorage.getItem('mode') as ModeSetting) || 'auto';
}

function getSystemMode(): Mode {
  return window.matchMedia('(prefers-color-scheme: dark)').matches
    ? 'dark'
    : 'light';
}

function resolveMode(setting: ModeSetting): Mode {
  return setting === 'auto' ? getSystemMode() : setting;
}

function applyTheme(colorTheme: ColorTheme, mode: Mode) {
  const root = document.documentElement;
  root.classList.remove(...ALL_THEME_CLASSES, 'light', 'dark');
  root.classList.add(`${colorTheme}-${mode}`, mode);
}

// Module-level listener: always reacts to OS theme changes when mode is 'auto'
window
  .matchMedia('(prefers-color-scheme: dark)')
  .addEventListener('change', () => {
    if (getStoredModeSetting() !== 'auto') return;
    applyTheme(getStoredColorTheme(), getSystemMode());
  });

export function useTheme() {
  const [colorTheme, setColorThemeState] =
    useState<ColorTheme>(getStoredColorTheme);
  const [modeSetting, setModeSettingState] =
    useState<ModeSetting>(getStoredModeSetting);
  const [resolvedMode, setResolvedMode] = useState<Mode>(() =>
    resolveMode(getStoredModeSetting()),
  );

  const setColorTheme = useCallback((newColorTheme: ColorTheme) => {
    localStorage.setItem('colorTheme', newColorTheme);
    applyTheme(newColorTheme, resolveMode(getStoredModeSetting()));
    setColorThemeState(newColorTheme);
  }, []);

  const setModeSetting = useCallback((newSetting: ModeSetting) => {
    localStorage.setItem('mode', newSetting);
    const resolved = resolveMode(newSetting);
    applyTheme(getStoredColorTheme(), resolved);
    setModeSettingState(newSetting);
    setResolvedMode(resolved);
  }, []);

  return {
    colorTheme,
    modeSetting,
    resolvedMode,
    setColorTheme,
    setModeSetting,
  } as const;
}
