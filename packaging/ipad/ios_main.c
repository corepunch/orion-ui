#include <platform/platform.h>

bool_t orion_ios_app_start(int argc, char **argv);
void orion_ios_app_frame(void);
void orion_ios_app_stop(void);

int main(int argc, char **argv) {
  return axRunApplication(argc, argv, orion_ios_app_start,
                          orion_ios_app_frame, orion_ios_app_stop);
}
