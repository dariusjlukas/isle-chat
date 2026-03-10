import { createContext, useContext } from 'react';

export const SettingsNavContext = createContext<(key: string) => void>(
  () => {},
);
export const useSettingsNav = () => useContext(SettingsNavContext);
