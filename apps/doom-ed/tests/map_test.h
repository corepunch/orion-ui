#ifndef MAP_TEST_H_
#define MAP_TEST_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

// Minimal types needed for triangulation testing
typedef struct {
  int16_t x;
  int16_t y;
} mapvertex_t;

typedef struct {
  int16_t x;
  int16_t y;
  int16_t z;
  int16_t u;
  int16_t v;
  int8_t nx;
  int8_t ny;
  int8_t nz;
  uint8_t color[4];
} wall_vertex_t;

#endif
