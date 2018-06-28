import * as m from 'mithril';
import track from './track';

export default {
    view () {
        return m('.frontend',
            { style: { border: "1px solid #ccc", padding: "20px" } }, [
            m('h1', "Frontend"),
            m(track)
        ]);
    }
} as m.Component;