import { TurboModuleRegistry, type TurboModule } from 'react-native';

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

export default TurboModuleRegistry.getEnforcing<Spec>('Perfetto');
