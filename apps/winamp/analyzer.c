// ANALYZER: Winamp's 19-bar spectrum from the last WA_VIS_N output samples,
// with bar falloff and slowly dropping peak dots.

#include "winamp.h"

static void fft(float *re, float *im, int n) {
  for (int i = 1, j = 0; i < n; i++) {
    int bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) { float t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; }
  }
  for (int len = 2; len <= n; len <<= 1) {
    float ang = -2.0f * (float)M_PI / len, wr = cosf(ang), wi = sinf(ang);
    for (int i = 0; i < n; i += len) {
      float cr = 1, ci = 0;
      for (int k = 0; k < len / 2; k++) {
        int a = i + k, b = a + len / 2;
        float xr = re[b] * cr - im[b] * ci, xi = re[b] * ci + im[b] * cr;
        re[b] = re[a] - xr; im[b] = im[a] - xi;
        re[a] += xr; im[a] += xi;
        float t = cr * wr - ci * wi;
        ci = cr * wi + ci * wr; cr = t;
      }
    }
  }
}

void analyzer_update(const float ring[WA_VIS_N], int at, uint8_t bars[WA_VIS_BARS], uint8_t peaks[WA_VIS_BARS],
                     float hold[WA_VIS_BARS], bool active) {
  float re[WA_VIS_N], im[WA_VIS_N];
  for (int i = 0; i < WA_VIS_N; i++) {
    float w = 0.5f - 0.5f * cosf(2.0f * (float)M_PI * i / (WA_VIS_N - 1));
    re[i] = active ? ring[(at + i) % WA_VIS_N] * w : 0;
    im[i] = 0;
  }
  fft(re, im, WA_VIS_N);
  // Log-spaced bins from ~60 Hz to ~16 kHz.
  for (int b = 0; b < WA_VIS_BARS; b++) {
    float f0 = 60.0f * powf(16000.0f / 60.0f, (float)b / WA_VIS_BARS);
    float f1 = 60.0f * powf(16000.0f / 60.0f, (float)(b + 1) / WA_VIS_BARS);
    int k0 = MAX(1, (int)(f0 * WA_VIS_N / WA_RATE)), k1 = MAX(k0 + 1, (int)(f1 * WA_VIS_N / WA_RATE));
    float m = 0;
    for (int k = k0; k < k1 && k < WA_VIS_N / 2; k++) m = MAX(m, sqrtf(re[k] * re[k] + im[k] * im[k]));
    float db = 20.0f * log10f(m + 1e-6f);
    int h = (int)((db + 10.0f) * 16.0f / 46.0f);
    h = MAX(0, MIN(16, h));
    bars[b] = (uint8_t)MAX(h, bars[b] > 0 ? bars[b] - 1 : 0);
    if (bars[b] >= peaks[b]) { peaks[b] = bars[b]; hold[b] = 0; }
    else if ((hold[b] += 0.08f) > 1.0f && peaks[b] > 0) peaks[b]--;
  }
}
