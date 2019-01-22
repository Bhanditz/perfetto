// Copyright (C) 2019 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

import {fromNs} from '../../common/time';
import {
  TrackController,
  trackControllerRegistry
} from '../../controller/track_controller';

import {
  Config,
  CPU_FREQ_TRACK_KIND,
  Data,
} from './common';

class CpuFreqTrackController extends TrackController<Config, Data> {
  static readonly kind = CPU_FREQ_TRACK_KIND;
  private busy = false;
  private setup = false;
  private maximumValueSeen = 0;
  private minimumValueSeen = 0;

  onBoundsChange(start: number, end: number, resolution: number): void {
    this.update(start, end, resolution);
  }

  private async update(start: number, end: number, resolution: number):
      Promise<void> {
    // TODO: we should really call TraceProcessor.Interrupt() at this point.
    if (this.busy) return;

    this.busy = true;
    if (!this.setup) {
      const result = await this.query(`
      select max(value), min(value) from
        counters where name = 'cpufreq'
        and ref = ${this.config.cpu}`);
      this.maximumValueSeen = +result.columns[0].doubleValues![0];
      this.minimumValueSeen = +result.columns[1].doubleValues![0];

      await this.query(
        `create virtual table ${this.tableName('window')} using window;`);

      await this.query(`create view ${this.tableName('freq')}
          as select
            ts,
            dur,
            ref as cpu,
            name as freq_name,
            value as freq_value
          from counters
          where name = 'cpufreq'
            and ref = ${this.config.cpu}
            and ref_type = 'cpu';
      `);

      await this.query(`create view ${this.tableName('idle')}
        as select
          ts,
          dur,
          ref as cpu,
          name as idle_name,
          value as idle_value
        from counters
        where name = 'cpuidle'
          and ref = ${this.config.cpu}
          and ref_type = 'cpu';
      `);

      await this.query(`create virtual table ${this.tableName('freq_idle')}
              using span_join(${this.tableName('freq')} PARTITIONED cpu,
                              ${this.tableName('idle')} PARTITIONED cpu);`);

      await this.query(`create virtual table ${this.tableName('span_activity')}
      using span_join(${this.tableName('freq_idle')} PARTITIONED cpu,
                      ${this.tableName('window')} PARTITIONED cpu);`);
      
      await this.query(`create view ${this.tableName('activity')}
      as select
        ts,
        dur,
        quantum_ts,
        cpu,
        case idle_value
          when 4294967295 then 0
          else 1
        end as idle,
        freq_value,
        idle_value
        from ${this.tableName('span_activity')};
      `);
    
      this.setup = true;
    }

    const query = `select ts, dur, idle, freq_value, idle_value from 
      ${this.tableName('activity')}`;

    const freqResult = await this.query(query);

    const numRows = +freqResult.numRecords;
    const data: Data = {
      start,
      end,
      maximumValue: this.maximumValue(),
      minimumValue: this.minimumValue(),
      resolution,
      tsStarts: new Float64Array(numRows),
      tsEnds: new Float64Array(numRows),
      freqKHz: new Uint32Array(numRows),
      idleValues: new Float64Array(numRows),
      idles: new Uint8Array(numRows),
    };

    const cols = freqResult.columns;
    for (let row = 0; row < numRows; row++) {
      const startSec = fromNs(+cols[0].longValues![row]);
      data.tsStarts[row] = startSec;
      data.tsEnds[row] = startSec + fromNs(+cols[1].longValues![row]);
      data.idles[row] = +cols[2].longValues![row];
      data.freqKHz[row] = +cols[3].doubleValues![row];
      data.idleValues[row] = +cols[4].doubleValues![row];
    }

    this.publish(data);
    this.busy = false;
  }

  private maximumValue() {
    return Math.max(this.config.maximumValue || 0, this.maximumValueSeen);
  }

  private minimumValue() {
    return Math.min(this.config.minimumValue || 0, this.minimumValueSeen);
  }

  private async query(query: string) {
    const result = await this.engine.query(query);
    if (result.error) {
      console.error(`Query error "${query}": ${result.error}`);
      throw new Error(`Query error "${query}": ${result.error}`);
    }
    return result;
  }
}

trackControllerRegistry.register(CpuFreqTrackController);