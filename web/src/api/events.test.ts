import { describe, expect, it } from 'vitest';
import { parseFrames } from './events.js';

describe('parsing the event stream', () => {
  it('reads one complete frame', () => {
    const { events, rest } = parseFrames(
      'id: 1\nevent: year_completed\ndata: {"type":"year_completed","year":2010}\n\n',
    );
    expect(events).toHaveLength(1);
    expect(events[0]).toMatchObject({ type: 'year_completed', year: 2010 });
    expect(rest).toBe('');
  });

  it('keeps a partial frame for the next chunk', () => {
    // A chunked response splits wherever the network feels like it, including mid-frame.
    const first = parseFrames('event: state\ndata: {"type":"sta');
    expect(first.events).toHaveLength(0);

    const second = parseFrames(first.rest + 'te","state":"running"}\n\n');
    expect(second.events).toHaveLength(1);
    expect(second.events[0]).toMatchObject({ type: 'state', state: 'running' });
  });

  it('reads several frames from one chunk', () => {
    const { events } = parseFrames(
      'data: {"type":"state","state":"running"}\n\n' +
        'data: {"type":"year_completed","year":2011}\n\n',
    );
    expect(events).toHaveLength(2);
  });

  it('ignores the heartbeat', () => {
    // The server writes a comment frame on an idle connection to keep it open through a proxy.
    const { events } = parseFrames(': heartbeat\n\ndata: {"type":"state","state":"running"}\n\n');
    expect(events).toHaveLength(1);
  });

  it('drops a frame that is not JSON rather than stopping the stream', () => {
    // A bad frame is a server bug; the next one is probably fine and the run is still going.
    const { events } = parseFrames(
      'data: not json\n\ndata: {"type":"state","state":"completed"}\n\n',
    );
    expect(events).toHaveLength(1);
    expect(events[0]).toMatchObject({ state: 'completed' });
  });

  it('accepts a data line with no space after the colon', () => {
    const { events } = parseFrames('data:{"type":"state","state":"running"}\n\n');
    expect(events).toHaveLength(1);
  });
});
