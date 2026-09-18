// A line chart, as inline SVG.
//
// About a hundred lines rather than a charting library, for a research tool whose readers want the
// number: a library's defaults — animated transitions, tooltips that round, a y axis that starts at
// zero because it looks tidier — are wrong here (ADR 0043).
//
// The scale computation is separate and pure, because that is the part where a mistake draws a
// plausible and wrong picture.
export interface Line {
  label: string;
  /** Parallel to `x`, with null for a year with no value. */
  values: Array<number | null>;
  colour: string;
  dashed?: boolean;
}

export interface Scale {
  min: number;
  max: number;
  /** The tick values, including the ends. */
  ticks: number[];
}

/**
 * The y scale for a set of lines.
 *
 * It does **not** start at zero unless the data does. A mean BMI moving from 24.9 to 25.4 is the
 * whole finding, and a chart with a zero baseline draws it as a flat line — which is a chart that
 * has decided what the reader should conclude.
 *
 * A series with no variation at all gets a band around its value rather than a degenerate scale,
 * because dividing by a zero range draws nothing.
 */
export function computeScale(lines: Line[], tickCount = 5): Scale {
  const values: number[] = [];
  for (const line of lines) {
    for (const value of line.values) {
      if (value !== null && Number.isFinite(value)) values.push(value);
    }
  }

  if (values.length === 0) return { min: 0, max: 1, ticks: [0, 1] };

  let min = Math.min(...values);
  let max = Math.max(...values);

  if (min === max) {
    const pad = Math.abs(min) > 0 ? Math.abs(min) * 0.05 : 1;
    min -= pad;
    max += pad;
  } else {
    // A little headroom, so a line never sits on the frame.
    const pad = (max - min) * 0.05;
    min -= pad;
    max += pad;
  }

  const ticks: number[] = [];
  const count = Math.max(2, tickCount);
  for (let i = 0; i < count; i += 1) {
    ticks.push(min + ((max - min) * i) / (count - 1));
  }
  return { min, max, ticks };
}

/** A number for an axis label: enough digits to tell the ticks apart, and no more. */
export function formatTick(value: number, range: number): string {
  if (!Number.isFinite(value)) return '';
  const magnitude = Math.abs(range);
  if (magnitude === 0) return value.toPrecision(3);
  if (magnitude < 0.001) return value.toExponential(2);
  const decimals = Math.max(0, Math.min(6, Math.ceil(-Math.log10(magnitude)) + 2));
  return value.toFixed(decimals);
}

export interface ChartOptions {
  width?: number;
  height?: number;
  xLabel?: string;
  yLabel?: string;
}

const PALETTE = ['#2b6cb0', '#c05621', '#2f855a', '#6b46c1', '#b83280', '#4a5568'];

export function colourFor(index: number): string {
  return PALETTE[index % PALETTE.length] as string;
}

/**
 * Renders the chart as an SVG element.
 *
 * A gap in a line is a gap, not a straight line through it: a year with no value is a year the
 * variable was not defined in, and joining across it would draw data that does not exist.
 */
export function renderChart(x: number[], lines: Line[], options: ChartOptions = {}): SVGSVGElement {
  const width = options.width ?? 720;
  const height = options.height ?? 300;
  const left = 64;
  const right = 12;
  const top = 12;
  const bottom = 34;

  const scale = computeScale(lines);
  const plotWidth = width - left - right;
  const plotHeight = height - top - bottom;

  const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
  svg.setAttribute('viewBox', `0 0 ${width} ${height}`);
  svg.setAttribute('class', 'chart');
  svg.setAttribute('role', 'img');
  svg.setAttribute('aria-label', options.yLabel ?? 'chart');

  const make = <K extends keyof SVGElementTagNameMap>(
    name: K,
    attributes: Record<string, string>,
  ): SVGElementTagNameMap[K] => {
    const element = document.createElementNS('http://www.w3.org/2000/svg', name);
    for (const [key, value] of Object.entries(attributes)) element.setAttribute(key, value);
    return element;
  };

  const xAt = (index: number): number =>
    x.length <= 1 ? left + plotWidth / 2 : left + (plotWidth * index) / (x.length - 1);
  const yAt = (value: number): number =>
    top + plotHeight - ((value - scale.min) / (scale.max - scale.min)) * plotHeight;

  const range = scale.max - scale.min;
  for (const tick of scale.ticks) {
    const y = yAt(tick);
    svg.appendChild(
      make('line', {
        x1: String(left), x2: String(left + plotWidth), y1: String(y), y2: String(y),
        class: 'chart-grid',
      }),
    );
    const label = make('text', {
      x: String(left - 8), y: String(y + 4), class: 'chart-axis', 'text-anchor': 'end',
    });
    label.textContent = formatTick(tick, range);
    svg.appendChild(label);
  }

  // At most eight x labels, so they never overlap whatever the horizon is.
  const step = Math.max(1, Math.ceil(x.length / 8));
  for (let i = 0; i < x.length; i += step) {
    const label = make('text', {
      x: String(xAt(i)), y: String(height - 12), class: 'chart-axis', 'text-anchor': 'middle',
    });
    label.textContent = String(x[i]);
    svg.appendChild(label);
  }

  for (const line of lines) {
    // Each run of consecutive defined values is its own path, so a gap stays a gap.
    let run: string[] = [];
    const flush = (): void => {
      if (run.length >= 1) {
        svg.appendChild(
          make('path', {
            d: run.join(' '),
            fill: 'none',
            stroke: line.colour,
            'stroke-width': '2',
            ...(line.dashed ? { 'stroke-dasharray': '5 3' } : {}),
          }),
        );
      }
      run = [];
    };

    line.values.forEach((value, index) => {
      if (value === null || !Number.isFinite(value)) {
        flush();
        return;
      }
      run.push(`${run.length === 0 ? 'M' : 'L'} ${xAt(index)} ${yAt(value)}`);
    });
    flush();
  }

  return svg;
}
