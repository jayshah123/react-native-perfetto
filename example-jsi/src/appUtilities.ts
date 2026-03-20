import { NativeModules } from 'react-native';

export interface AppUtilities {
  math: {
    add(left: number, right: number): number;
    countPrimes(limit: number): number;
  };
  logic: {
    isPalindrome(value: string): boolean;
  };
  cache: {
    writeText(key: string, contents: string): void;
    readText(key: string): string | null;
    remove(key: string): boolean;
  };
  diagnostics: {
    captureTrace(): string;
  };
}

type InstallerModule = {
  install: () => boolean;
};

declare global {
  var __appUtilities: AppUtilities | undefined;
}

let installAttempted = false;

function getInstallerModule(): InstallerModule {
  const installer = NativeModules.AppUtilitiesInstaller as
    | InstallerModule
    | undefined;

  if (!installer || typeof installer.install !== 'function') {
    throw new Error('Native utility installer module is unavailable.');
  }

  return installer;
}

export function ensureAppUtilitiesInstalled(): void {
  if (global.__appUtilities) {
    installAttempted = true;
    return;
  }

  const installer = getInstallerModule();
  const installed = installer.install();
  installAttempted = true;

  if (!installed || !global.__appUtilities) {
    throw new Error('Failed to install app utility bindings.');
  }
}

export function getAppUtilities(): AppUtilities {
  if (!global.__appUtilities) {
    if (!installAttempted) {
      ensureAppUtilitiesInstalled();
    }
  }

  const utilities = global.__appUtilities;
  if (!utilities) {
    throw new Error('App utility bindings are not available.');
  }

  return utilities;
}
