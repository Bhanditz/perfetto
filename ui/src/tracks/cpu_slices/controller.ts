// Copyright (C) 2018 The Android Open Source Project
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
  CPU_SLICE_TRACK_KIND,
  Data,
  SliceData,
  SummaryData
} from './common';

class CpuSliceTrackController extends TrackController<Config, Data> {
  static readonly kind = CPU_SLICE_TRACK_KIND;
  private busy = false;
  private setup = false;

  onBoundsChange(start: number, end: number, resolution: number): void {
    this.update(start, end, resolution);
  }

  private async update(start: number, end: number, resolution: number):
      Promise<void> {
    // TODO: we should really call TraceProcessor.Interrupt() at this point.
    if (this.busy) return;

    const startNs = Math.round(start * 1e9);
    const endNs = Math.round(end * 1e9);

    this.busy = true;
    if (this.setup === false) {
      await this.query(
          `create virtual table ${this.tableName('window')} using window;`);

      await this.query(`create virtual table ${this.tableName('span_sched')}
              using span_join(sched PARTITIONED cpu,
                              ${this.tableName('window')} PARTITIONED cpu);`);

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
            when 4294967295 then "freq"
            else "idle"
          end as name,
          case idle_value
            when 4294967295 then freq_value
            else idle_value
          end as value
          from ${this.tableName('span_activity')};
      `);

      this.setup = true;
    }

    // |resolution| is in s/px (to nearest power of 10) asumming a display
    // of ~1000px 0.001 is 1s.
    const isQuantized = resolution >= 0.001;
    // |resolution| is in s/px we want # ns for 10px window:
    const bucketSizeNs = Math.round(resolution * 10 * 1e9);
    let windowStartNs = startNs;
    if (isQuantized) {
      windowStartNs = Math.floor(windowStartNs / bucketSizeNs) * bucketSizeNs;
    }
    const windowDurNs = Math.max(1, endNs - windowStartNs);

    this.query(`update window_${this.trackState.id} set
      window_start=${windowStartNs},
      window_dur=${windowDurNs},
      quantum=${isQuantized ? bucketSizeNs : 0}
      where rowid = 0;`);

    if (isQuantized) {
      this.publish(await this.computeSummary(
          fromNs(windowStartNs), end, resolution, bucketSizeNs));
    } else {
      this.publish(
          await this.computeSlices(fromNs(windowStartNs), end, resolution));
    }
    this.busy = false;
  }

  private async computeSummary(
      start: number, end: number, resolution: number,
      bucketSizeNs: number): Promise<SummaryData> {
    const startNs = Math.round(start * 1e9);
    const endNs = Math.round(end * 1e9);
    const numBuckets = Math.ceil((endNs - startNs) / bucketSizeNs);

    const utilizationQuery = `select
        quantum_ts as bucket,
        sum(dur)/cast(${bucketSizeNs} as float) as utilization
        from ${this.tableName('span_sched')}
        where cpu = ${this.config.cpu}
        and utid != 0
        group by quantum_ts`;

    const freqQuery = `select
        quantum_ts as bucket,
        name,
        max(value) as value
        from ${this.tableName('activity')}
        where cpu = ${this.config.cpu}
        group by quantum_ts, name`;

    const [utilizationResult, freqResult] = await Promise.all([
      this.query(utilizationQuery),
      this.query(freqQuery),
    ]);

    const summary: Data = {
      kind: 'summary',
      start,
      end,
      resolution,
      bucketSizeSeconds: fromNs(bucketSizeNs),
      utilizations: new Float64Array(numBuckets),
      freqs: new Float64Array(numBuckets),
      idles: new Float64Array(numBuckets),
    };
    const utilizationCols = utilizationResult.columns;
    const freqCols = freqResult.columns;
    for (let row = 0; row < utilizationResult.numRecords; row++) {
      const bucket = +utilizationCols[0].longValues![row];
      summary.utilizations[bucket] = +utilizationCols[1].doubleValues![row];
    }

    for (let row = 0; row < freqResult.numRecords; row++) {
      const bucket = +freqCols[0].longValues![row];
      if (freqCols[1].stringValues![row] === 'idle') {
        summary.idles[bucket] = +freqCols[2].doubleValues![row];
      } else {
        summary.freqs[bucket] = +freqCols[2].doubleValues![row];
      }
    }
    return summary;
  }

  private async computeSlices(start: number, end: number, resolution: number):
      Promise<SliceData> {
    // TODO(hjd): Remove LIMIT
    const LIMIT = 10000;

    const sliceQuery = `select ts,dur,utid from ${this.tableName('span_sched')}
        where cpu = ${this.config.cpu}
        and utid != 0
        limit ${LIMIT};`;

    const freqQuery = `select
        ts,
        case name
            when 'freq' then value
            else 0.0
        end as freq_or_zero
        from ${this.tableName('activity')}
        where cpu = ${this.config.cpu}`;

    const [sliceResult, freqResult] = await Promise.all([
      this.query(sliceQuery),
      this.query(freqQuery),
    ]);

    const numSliceRows = +sliceResult.numRecords;
    const numFreqRows = +freqResult.numRecords;
    const slices: SliceData = {
      kind: 'slice',
      start,
      end,
      resolution,
      starts: new Float64Array(numSliceRows),
      ends: new Float64Array(numSliceRows),
      utids: new Uint32Array(numSliceRows),
      freqStarts: new Float64Array(numFreqRows),
      freqs: new Float64Array(numFreqRows),
    };

    {
      const cols = sliceResult.columns;
      for (let row = 0; row < numSliceRows; row++) {
        const startSec = fromNs(+cols[0].longValues![row]);
        slices.starts[row] = startSec;
        slices.ends[row] = startSec + fromNs(+cols[1].longValues![row]);
        slices.utids[row] = +cols[2].longValues![row];
      }
      if (numSliceRows === LIMIT) {
        slices.end = slices.ends[slices.ends.length - 1];
      }
    }

    {
      const cols = freqResult.columns;
      for (let row = 0; row < numFreqRows; row++) {
        slices.freqStarts[row] = fromNs(+cols[0].longValues![row]);
        slices.freqs[row] = +cols[1].doubleValues![row];
      }
      console.log(freqResult);
      console.log(slices);
    }

    return slices;
  }

  private async query(query: string) {
    const result = await this.engine.query(query);
    if (result.error) {
      console.error(`Query error "${query}": ${result.error}`);
      throw new Error(`Query error "${query}": ${result.error}`);
    }
    return result;
  }

  onDestroy(): void {
    if (this.setup) {
      //this.query(`drop table ${this.tableName('span_counters')}`);
      //this.query(`drop table ${this.tableName('freq_view')}`);
      //this.query(`drop table ${this.tableName('span_sched')}`);
      //this.query(`drop table ${this.tableName('window')}`);
      this.setup = false;
    }
  }
}

trackControllerRegistry.register(CpuSliceTrackController);
