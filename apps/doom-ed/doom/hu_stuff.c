#include <stdio.h>

#include <doom/d_englsh.h>

#define HU_TITLE2  (mapnames2[gamemap-1])


char* mapnames[] =
{
  HUSTR_E1M1,
  HUSTR_E1M2,
  HUSTR_E1M3,
  HUSTR_E1M4,
  HUSTR_E1M5,
  HUSTR_E1M6,
  HUSTR_E1M7,
  HUSTR_E1M8,
  HUSTR_E1M9,
  HUSTR_E2M1,
  HUSTR_E2M2,
  HUSTR_E2M3,
  HUSTR_E2M4,
  HUSTR_E2M5,
  HUSTR_E2M6,
  HUSTR_E2M7,
  HUSTR_E2M8,
  HUSTR_E2M9,
  HUSTR_E3M1,
  HUSTR_E3M2,
  HUSTR_E3M3,
  HUSTR_E3M4,
  HUSTR_E3M5,
  HUSTR_E3M6,
  HUSTR_E3M7,
  HUSTR_E3M8,
  HUSTR_E3M9,
  HUSTR_E4M1,
  HUSTR_E4M2,
  HUSTR_E4M3,
  HUSTR_E4M4,
  HUSTR_E4M5,
  HUSTR_E4M6,
  HUSTR_E4M7,
  HUSTR_E4M8,
  HUSTR_E4M9,
};

char*  mapnames2[] =  // DOOM 2 map names.
{
  HUSTR_1,
  HUSTR_2,
  HUSTR_3,
  HUSTR_4,
  HUSTR_5,
  HUSTR_6,
  HUSTR_7,
  HUSTR_8,
  HUSTR_9,
  HUSTR_10,
  HUSTR_11,
  
  HUSTR_12,
  HUSTR_13,
  HUSTR_14,
  HUSTR_15,
  HUSTR_16,
  HUSTR_17,
  HUSTR_18,
  HUSTR_19,
  HUSTR_20,
  
  HUSTR_21,
  HUSTR_22,
  HUSTR_23,
  HUSTR_24,
  HUSTR_25,
  HUSTR_26,
  HUSTR_27,
  HUSTR_28,
  HUSTR_29,
  HUSTR_30,
  HUSTR_31,
  HUSTR_32,
};

const char *get_map_name(const char *name) {
  int e = -1, m = -1;
  sscanf(name, "E%dM%d", &e, &m);
  if (e > 0 && e <= 9 && m > 0 && m <= 9) {
    return mapnames2[(e-1)*9+m-1];
//  int index = -1;
//  sscanf(name, "MAP%d", &index);
//  if (index > 0 && index <= sizeof(mapnames2)/sizeof(*mapnames2)) {
//    return mapnames2[index - 1];
  } else {
    return "Unknown map";
  }
}
