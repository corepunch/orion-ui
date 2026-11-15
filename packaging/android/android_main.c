// Android entry point. NativeActivity runs android_main() on its own thread;
// it unpacks the bundled files to private storage once per build (Orion reads
// real paths, laid out as bin/../share like a desktop install), then runs the
// app's ordinary blocking loop, which GEM_STANDALONE_MAIN names orion_main().
#include <android/asset_manager.h>
#include <android/log.h>
#include <android_native_app_glue.h>
#include <errno.h>
#include <jni.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <platform/android/ax_android.h>

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "orion", __VA_ARGS__)

int orion_main(int argc, char *argv[]);

// Java → native, on the UI thread; the platform queues them for the app thread.
JNIEXPORT void JNICALL
Java_org_orion_OrionActivity_nativeText(JNIEnv *env, jclass type, jstring text)
{
  (void)type;
  char const *utf8 = text ? (*env)->GetStringUTFChars(env, text, NULL) : NULL;
  if (!utf8) return;
  axAndroidPostText(utf8);
  (*env)->ReleaseStringUTFChars(env, text, utf8);
}

JNIEXPORT void JNICALL
Java_org_orion_OrionActivity_nativeKey(JNIEnv *env, jclass type, jint key)
{
  (void)env; (void)type;
  axAndroidPostKey(key);
}

JNIEXPORT void JNICALL
Java_org_orion_OrionActivity_nativeInsets(JNIEnv *env, jclass type, jfloat left, jfloat top,
                                          jfloat right, jfloat bottom, jfloat keyboard)
{
  (void)env; (void)type;
  axAndroidSetInsets(left, top, right, bottom, keyboard);
}

static int
make_parents(char *path)
{
  for (char *slash = strchr(path + 1, '/'); slash; slash = strchr(slash + 1, '/')) {
    *slash = '\0';
    int failed = mkdir(path, 0700) && errno != EEXIST;
    *slash = '/';
    if (failed) return 0;
  }
  return 1;
}

static char *
asset_text(AAssetManager *assets, char const *name)
{
  AAsset *asset = AAssetManager_open(assets, name, AASSET_MODE_BUFFER);
  if (!asset) return NULL;
  off_t length = AAsset_getLength(asset);
  char *text = malloc((size_t)length + 1);
  if (text) { memcpy(text, AAsset_getBuffer(asset), (size_t)length); text[length] = '\0'; }
  AAsset_close(asset);
  return text;
}

// orion-files.txt lists every bundled file; orion-version.txt changes with their contents.
static int
unpack_assets(AAssetManager *assets, char const *root)
{
  char stamp[1024], current[256] = {0};
  snprintf(stamp, sizeof(stamp), "%s/.orion-version", root);
  char *version = asset_text(assets, "orion-version.txt");
  if (!version) { LOG("The APK has no orion-version.txt"); return 0; }
  FILE *f = fopen(stamp, "r");
  if (f) { if (!fgets(current, sizeof(current), f)) current[0] = '\0'; fclose(f); }
  if (!strcmp(current, version)) { free(version); return 1; }
  char *list = asset_text(assets, "orion-files.txt");
  int ok = list != NULL;
  for (char *line = list ? strtok(list, "\n") : NULL; ok && line; line = strtok(NULL, "\n")) {
    if (!*line || strstr(line, "..")) continue;
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", root, line);
    AAsset *asset = AAssetManager_open(assets, line, AASSET_MODE_STREAMING);
    FILE *out = asset && make_parents(path) ? fopen(path, "wb") : NULL;
    char buffer[16384];
    int n;
    while (out && (n = AAsset_read(asset, buffer, sizeof(buffer))) > 0)
      if (fwrite(buffer, 1, (size_t)n, out) != (size_t)n) ok = 0;
    if (!out) { ok = 0; LOG("Could not unpack %s", line); }
    if (out && fclose(out)) ok = 0;
    if (asset) AAsset_close(asset);
  }
  free(list);
  if (ok && (f = fopen(stamp, "w"))) { fputs(version, f); fclose(f); }
  free(version);
  return ok;
}

// Android discards stdout and stderr; forward their lines (framework
// diagnostics) to logcat: adb logcat -s orion
static void *
forward_stdio(void *fd)
{
  FILE *in = fdopen((int)(intptr_t)fd, "r");
  char line[1024];
  while (in && fgets(line, sizeof(line), in)) {
    line[strcspn(line, "\n")] = '\0';
    __android_log_write(ANDROID_LOG_INFO, "orion", line);
  }
  return NULL;
}

static void
redirect_stdio(void)
{
  int fds[2];
  pthread_t thread;
  if (pipe(fds)) return;
  setvbuf(stdout, NULL, _IOLBF, 0);
  setvbuf(stderr, NULL, _IONBF, 0);
  dup2(fds[1], STDOUT_FILENO);
  dup2(fds[1], STDERR_FILENO);
  close(fds[1]);
  if (!pthread_create(&thread, NULL, forward_stdio, (void *)(intptr_t)fds[0])) pthread_detach(thread);
}

static void
run(struct android_app *app)
{
  char const *root = app->activity->internalDataPath;
  if (!root || !unpack_assets(app->activity->assetManager, root)) {
    LOG("Could not unpack the bundled application files");
    return;
  }
  char bin[1024], exe[1024];
  snprintf(bin, sizeof(bin), "%s/bin", root);
  snprintf(exe, sizeof(exe), "%s/" ORION_ANDROID_APP, bin);
  if (mkdir(bin, 0700) && errno != EEXIST) { LOG("Cannot create %s", bin); return; }
  // Documents and HOME are the app's external files directory, visible over USB.
  char const *documents = app->activity->externalDataPath ? app->activity->externalDataPath : root;
  if (documents && documents[0]) {
    setenv("HOME", documents, 1);
    if (chdir(documents)) LOG("Cannot enter %s", documents);
  }
  char *argv[] = { exe, NULL };
  int status = orion_main(1, argv);
  if (status) LOG("%s exited with status %d", ORION_ANDROID_APP, status);
}

void
android_main(struct android_app *app)
{
  redirect_stdio();
  axAndroidSetApp(app);
  run(app);
  ANativeActivity_finish(app->activity);
  while (!app->destroyRequested) {
    int events;
    struct android_poll_source *source = NULL;
    if (ALooper_pollOnce(-1, NULL, &events, (void **)&source) >= 0 && source) source->process(app, source);
  }
  // axSetTextInput attached this thread to Java.
  JavaVM *vm = app->activity->vm;
  (*vm)->DetachCurrentThread(vm);
  // The framework keeps process-wide state: a relaunch starts a fresh process.
  exit(0);
}
