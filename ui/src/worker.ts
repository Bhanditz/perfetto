/*
 * Copyright (C) 2018 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

import { Action, isUpdateQuery } from "./actions";
import {createZeroState, State} from './state';

console.log('Hello from the worker!');

class StateStore {
  private state: State;
  constructor() {
    this.state = createZeroState();
  }

  dispatch(action: Action) {
    if (isUpdateQuery(action)) {
      console.log(action.query);
      this.state.query = action.query;
    }
    (self as any).postMessage(this.state);
  }
}

const store = new StateStore();

self.onmessage = (message: MessageEvent) => {
  const action = message.data as Action;
  store.dispatch(action);

};

