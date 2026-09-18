// Reading the server's event stream.
//
// `EventSource` would do most of this, and is not used, for one reason: it retries for ever and
// gives no way to say "the run is over, stop". The stream here ends after `run_completed`, and a
// client that kept reconnecting would poll a finished run until the tab closed. Reading the body
// with `fetch` means the end of the body is the end of the stream.
import type { RunEvent } from './types.js';

export interface StreamHandlers {
  onEvent: (event: RunEvent) => void;
  onError?: (error: unknown) => void;
  onEnd?: () => void;
}

/** A stream that can be stopped. */
export interface Stream {
  close: () => void;
}

/**
 * Parses a server-sent event stream.
 *
 * Only what the server sends is handled: `id:`, `event:`, `data:` and comment lines. Multi-line
 * `data:` is joined with a newline, as the specification says, though nothing here sends one.
 */
export function parseFrames(buffer: string): { events: RunEvent[]; rest: string } {
  const events: RunEvent[] = [];
  let rest = buffer;

  for (;;) {
    const boundary = rest.indexOf('\n\n');
    if (boundary === -1) break;

    const frame = rest.slice(0, boundary);
    rest = rest.slice(boundary + 2);

    const data: string[] = [];
    for (const line of frame.split('\n')) {
      if (line.startsWith(':')) continue; // a comment; the heartbeat is one
      if (line.startsWith('data:')) {
        data.push(line.slice(5).replace(/^ /, ''));
      }
    }
    if (data.length === 0) continue;

    try {
      events.push(JSON.parse(data.join('\n')) as RunEvent);
    } catch {
      // A frame that is not JSON is a server bug, and dropping it is better than stopping the
      // stream: the next frame is probably fine and the run is still going.
    }
  }

  return { events, rest };
}

/** Opens the stream for a run and delivers its events until it ends or `close` is called. */
export function openRunStream(url: string, handlers: StreamHandlers): Stream {
  const controller = new AbortController();

  void (async () => {
    try {
      const response = await fetch(url, {
        signal: controller.signal,
        headers: { Accept: 'text/event-stream' },
      });
      if (!response.ok || !response.body) {
        handlers.onError?.(new Error(`the event stream returned ${response.status}`));
        handlers.onEnd?.();
        return;
      }

      const reader = response.body.getReader();
      const decoder = new TextDecoder();
      let buffer = '';

      for (;;) {
        const { done, value } = await reader.read();
        if (done) break;
        buffer += decoder.decode(value, { stream: true });
        const { events, rest } = parseFrames(buffer);
        buffer = rest;
        for (const event of events) handlers.onEvent(event);
      }
      handlers.onEnd?.();
    } catch (error) {
      if ((error as { name?: string })?.name === 'AbortError') {
        handlers.onEnd?.();
        return;
      }
      handlers.onError?.(error);
      handlers.onEnd?.();
    }
  })();

  return { close: () => controller.abort() };
}
