import { describe, expect, it } from 'vitest';
import { computeScale, formatTick, type Line } from './chart.js';

const line = (values: Array<number | null>): Line => ({ label: 'x', values, colour: '#000' });

describe('the chart scale', () => {
  it('does not start at zero unless the data does', () => {
    // A mean BMI moving from 24.9 to 25.4 is the whole finding; a zero baseline draws it flat,
    // which is a chart that has decided what the reader should conclude.
    const scale = computeScale([line([24.9, 25.1, 25.4])]);
    expect(scale.min).toBeGreaterThan(24);
    expect(scale.max).toBeLessThan(26);
  });

  it('covers every line', () => {
    const scale = computeScale([line([1, 2]), line([10, 20])]);
    expect(scale.min).toBeLessThanOrEqual(1);
    expect(scale.max).toBeGreaterThanOrEqual(20);
  });

  it('ignores the gaps', () => {
    const scale = computeScale([line([null, 5, null, 7, null])]);
    expect(scale.min).toBeLessThanOrEqual(5);
    expect(scale.max).toBeGreaterThanOrEqual(7);
  });

  it('gives a flat series a band rather than a degenerate scale', () => {
    // Dividing by a zero range draws nothing at all.
    const scale = computeScale([line([3, 3, 3])]);
    expect(scale.max).toBeGreaterThan(scale.min);
    expect(scale.min).toBeLessThan(3);
    expect(scale.max).toBeGreaterThan(3);
  });

  it('survives a series of zeroes', () => {
    const scale = computeScale([line([0, 0])]);
    expect(scale.max).toBeGreaterThan(scale.min);
  });

  it('survives having nothing to draw', () => {
    const scale = computeScale([]);
    expect(scale.max).toBeGreaterThan(scale.min);
    expect(computeScale([line([null, null])]).max).toBeGreaterThan(0);
  });

  it('puts the ticks at the ends and evenly between', () => {
    const scale = computeScale([line([0, 10])], 5);
    expect(scale.ticks).toHaveLength(5);
    expect(scale.ticks[0]).toBeCloseTo(scale.min);
    expect(scale.ticks[4]).toBeCloseTo(scale.max);
    const gaps = scale.ticks.slice(1).map((t, i) => t - (scale.ticks[i] as number));
    for (const gap of gaps) expect(gap).toBeCloseTo(gaps[0] as number);
  });

  it('is unaffected by a value that is not finite', () => {
    const scale = computeScale([line([1, Number.NaN, 3, Number.POSITIVE_INFINITY])]);
    expect(Number.isFinite(scale.min)).toBe(true);
    expect(Number.isFinite(scale.max)).toBe(true);
    expect(scale.max).toBeLessThan(10);
  });
});

describe('axis labels', () => {
  it('shows enough digits to tell the ticks apart', () => {
    // A range of 0.5 on a mean BMI: "25" on every tick would be useless.
    expect(formatTick(25.12345, 0.5)).toBe('25.123');
    expect(formatTick(1500, 1000)).toBe('1500');
  });

  it('falls back to exponent notation for a very small range', () => {
    expect(formatTick(0.0000123, 0.00001)).toContain('e');
  });

  it('survives a zero range and a value that is not finite', () => {
    expect(formatTick(5, 0)).toBe('5.00');
    expect(formatTick(Number.NaN, 1)).toBe('');
  });
});
