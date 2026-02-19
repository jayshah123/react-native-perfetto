import {
  NativeModules,
  Platform,
  TurboModuleRegistry,
  type TurboModule,
} from 'react-native';

export interface Spec extends TurboModule {
  isPerfettoSdkAvailable(): boolean;
  startRecording(
    filePath: string,
    bufferSizeKb: number,
    durationMs: number,
    backend: string
  ): Promise<boolean>;
  stopRecording(): Promise<string>;
  beginSection(category: string, name: string, argsJson: string): void;
  endSection(): void;
  instantEvent(category: string, name: string, argsJson: string): void;
  setCounter(
    category: string,
    name: string,
    value: number,
    argsJson: string
  ): void;
}

const LINKING_ERROR =
  `The package 'react-native-perfetto' doesn't seem to be linked. Make sure:\n\n` +
  Platform.select({ ios: "- You have run 'pod install'\n", default: '' }) +
  '- You rebuilt the app after installing the package\n' +
  '- You are not using Expo Go\n';

const PerfettoModule: Spec | null | undefined =
  TurboModuleRegistry.get<Spec>('Perfetto') ??
  (NativeModules.Perfetto as Spec | undefined);

const PerfettoModuleProxy = new Proxy(
  {},
  {
    get() {
      throw new Error(LINKING_ERROR);
    },
  }
) as Spec;

export default PerfettoModule ?? PerfettoModuleProxy;
