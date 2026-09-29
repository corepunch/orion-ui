#include "scener.h"
#include "reel.h"
#include <errno.h>
#include <sys/stat.h>
#include <orion/gem.h>
#include <orion/ui.h>
#include <orion/user/gl_compat.h>
#include <orion/user/image.h>
#include <orion/user/bmp_icon_loader.h>
#include <platform/platform.h>
#include <ctype.h>

#define DEFAULT_FOV   60.0f
#define SCENER_VERSION "1.1 Book CLI"
#define CLI_MAX_SIZE 8192
#define CLI_JPEG_QUALITY 95
#define LAYOUT_PADDING 1.08f
#define LAYOUT_CUT_FRACTION 0.85f
#define LAYOUT_EYE_OFFSET 10.0f
#define CLI_DEFAULT_SUPERSAMPLE 2
#define CLI_MAX_SUPERSAMPLE 4
#define CLI_BYTE_MAX 255.0f
#define CLI_COLOR_GAMMA 2.2f
#define CLI_MAX_FRAMES 100000
#define CLI_FRAME_EPSILON 0.0001f

typedef struct {
	bool screenshot_mode;
	bool batch, layout, list_cameras, list_joints, help, version, invalid;
	float time, frame_start, frame_end, fps;
	bool frames, time_set, check, output_dir_set;
	char reel_path[1024], poster_path[1024];
	char output_dir[1024], format[8];
	float layout_scale;
	bool debug_flags_set;
	char scene_path[512];
	char output_path[1024];
	char camera_name[MAX_CAMERA_NAME];
	int width, height, supersample;
	int debug_flags;
} scener_cli_t;

app_state_t *g_app = NULL;
static scener_cli_t g_cli;

// Navigation uses a reduced context-specific table while the viewport is
// being dragged. The default command accelerators are generated from the
// menu declarations in scener.orion below.
static const accel_t kNavigationAccelEntries[] = {
  { FCONTROL|FVIRTKEY, AX_KEY_Z, ID_EDIT_UNDO },
  { FCONTROL|FVIRTKEY, AX_KEY_Y, ID_EDIT_REDO },
  { FCONTROL|FVIRTKEY, AX_KEY_N, ID_FILE_NEW  },
  { FCONTROL|FVIRTKEY, AX_KEY_O, ID_FILE_OPEN },
  { FCONTROL|FVIRTKEY, AX_KEY_S, ID_FILE_SAVE },
  { FCONTROL|FVIRTKEY, AX_KEY_W, ID_FILE_CLOSE},
  { FCONTROL|FVIRTKEY, AX_KEY_D, ID_EDIT_DUPLICATE },
};
#define kNavigationAccelCount (int)(sizeof(kNavigationAccelEntries)/sizeof(kNavigationAccelEntries[0]))

accel_table_t *scener_active_accelerators(void) {
  if (!g_app) return NULL;
  return g_app->viewport_navigating ? g_app->navigation_accel : g_app->accel;
}

#ifndef BUILD_AS_GEM
static void cli_usage(void) {
	puts("scener [SCENE] [--cam NAME]\n"
	     "scener --render SCENE [--camera NAME] [--size WIDTHxHEIGHT] [--format jpg|png] [--output-dir DIR]\n"
	     "scener --layout SCENE [--scale PIXELS_PER_CM] [--format jpg|png] [--output-dir DIR]\n"
	     "scener --list-cameras SCENE\n"
	     "scener SCENE --screenshot FILE [--cam NAME] [--size WIDTHxHEIGHT]\n"
	     "scener --list-joints SCENE [--camera NAME] [--time SECONDS]\n"
	     "scener --reel REEL (--output FILE.mp4|FILE.png | --output-dir DIR) [--time SECONDS] [--poster FILE.png]\n"
	     "scener --reel REEL --check\n"
	     "scener --render SCENE --camera NAME --frames START:END:FPS --output FILE.mp4 [--size WIDTHxHEIGHT]\n"
	     "Options: --time SECONDS, --frames START:END:FPS (with --render), --supersample 1..4 (default 2),\n"
	     "  -no-shadows, -wireframe, -d FLAGS, --help, --version\n"
	     "Scenes default to Y up; <scene up=\"z\"> selects Z-up views without changing primitive axes.");
}
#endif

static void cli_parse(int argc, char *argv[]) {
	memset(&g_cli,0,sizeof(g_cli));
	g_cli.width=1280; g_cli.height=800; g_cli.layout_scale=2;g_cli.supersample=CLI_DEFAULT_SUPERSAMPLE;
	strcpy(g_cli.format,"jpg"); strcpy(g_cli.output_dir,".");
	g_cli.debug_flags=DBG_HIDE_CHARS|DBG_HIDE_LIGHTS;
	for(int i=1;i<argc;i++){
		const char *arg=argv[i], *value=NULL;
		if(!strcmp(arg,"--help")||!strcmp(arg,"-h")){g_cli.help=true;continue;}
		if(!strcmp(arg,"--version")||!strcmp(arg,"-V")){g_cli.version=true;continue;}
		if(!strcmp(arg,"--render")){g_cli.batch=g_cli.screenshot_mode=true;continue;}
		if(!strcmp(arg,"--layout")){g_cli.layout=g_cli.screenshot_mode=true;continue;}
		if(!strcmp(arg,"--list-cameras")||!strcmp(arg,"-list-cameras")){g_cli.list_cameras=true;continue;}
		if(!strcmp(arg,"--list-joints")){g_cli.list_joints=true;continue;}
		if(!strcmp(arg,"-no-shadows")){g_cli.debug_flags|=DBG_NO_SHADOWS;continue;}
		if(!strcmp(arg,"-wireframe")){g_cli.debug_flags|=DBG_WIREFRAME;continue;}
		if(!strcmp(arg,"--check")){g_cli.check=true;continue;}
		if(arg[0]!='-'){
			if(g_cli.scene_path[0]){fprintf(stderr,"unexpected argument: %s\n",arg);g_cli.invalid=true;}
			else snprintf(g_cli.scene_path,sizeof(g_cli.scene_path),"%s",arg);
			continue;
		}
		bool output=!strcmp(arg,"--screenshot")||!strcmp(arg,"--output")||!strcmp(arg,"-o");
		bool output_dir=!strcmp(arg,"--output-dir")||(!strcmp(arg,"-o")&&(g_cli.batch||g_cli.layout));
		if(output_dir)output=false;
		bool camera=!strcmp(arg,"--camera")||!strcmp(arg,"--cam")||!strcmp(arg,"-cam");
		bool known=output||output_dir||camera||!strcmp(arg,"--reel")||!strcmp(arg,"--poster")||!strcmp(arg,"--size")||!strcmp(arg,"--format")||!strcmp(arg,"--output-dir")||!strcmp(arg,"--scale")||!strcmp(arg,"--supersample")||!strcmp(arg,"-d")||!strcmp(arg,"--time")||!strcmp(arg,"--frames");
		if(!known){fprintf(stderr,"unsupported option: %s\n",arg);g_cli.invalid=true;continue;}
		if(i+1>=argc){fprintf(stderr,"missing value for %s\n",arg);g_cli.invalid=true;continue;}
		value=argv[++i];
		if(!strcmp(arg,"--reel")){snprintf(g_cli.reel_path,sizeof(g_cli.reel_path),"%s",value);continue;}
		if(!strcmp(arg,"--poster")){snprintf(g_cli.poster_path,sizeof(g_cli.poster_path),"%s",value);continue;}
		if(output){g_cli.screenshot_mode=true;snprintf(g_cli.output_path,sizeof(g_cli.output_path),"%s",value);}
		else if(camera) snprintf(g_cli.camera_name,sizeof(g_cli.camera_name),"%s",value);
		else if(output_dir){g_cli.output_dir_set=true;snprintf(g_cli.output_dir,sizeof(g_cli.output_dir),"%s",value);}
		else if(!strcmp(arg,"--format")){
			if(strcasecmp(value,"jpg")&&strcasecmp(value,"jpeg")&&strcasecmp(value,"png")){fprintf(stderr,"unsupported format: %s\n",value);g_cli.invalid=true;}
			else snprintf(g_cli.format,sizeof(g_cli.format),"%s",!strcasecmp(value,"png")?"png":"jpg");
		}else if(!strcmp(arg,"--size")){
			char tail; int w,h;
			if(sscanf(value,"%dx%d%c",&w,&h,&tail)!=2||w<=0||h<=0||w>CLI_MAX_SIZE||h>CLI_MAX_SIZE){fprintf(stderr,"invalid size: %s\n",value);g_cli.invalid=true;}
			else{g_cli.width=w;g_cli.height=h;}
		}else if(!strcmp(arg,"--scale")){
			char *end; float scale=strtof(value,&end);
			if(*end||!isfinite(scale)||scale<=0){fprintf(stderr,"invalid scale: %s\n",value);g_cli.invalid=true;}else g_cli.layout_scale=scale;
		}else if(!strcmp(arg,"--supersample")){
			char *end;long n=strtol(value,&end,10);
			if(*end||n<1||n>CLI_MAX_SUPERSAMPLE){fprintf(stderr,"invalid supersampling: %s\n",value);g_cli.invalid=true;}else g_cli.supersample=(int)n;
		}else if(!strcmp(arg,"--time")){
			char *end; float t=strtof(value,&end);
			if(*end||!isfinite(t)){fprintf(stderr,"invalid time: %s\n",value);g_cli.invalid=true;}else{g_cli.time=t;g_cli.time_set=true;}
		}else if(!strcmp(arg,"--frames")){
			char tail; float a,b,f;
			if(sscanf(value,"%f:%f:%f%c",&a,&b,&f,&tail)!=3||!isfinite(a)||!isfinite(b)||b<a||f<=0||(b-a)*f>CLI_MAX_FRAMES){fprintf(stderr,"invalid frames (START:END:FPS): %s\n",value);g_cli.invalid=true;}
			else{g_cli.frames=true;g_cli.frame_start=a;g_cli.frame_end=b;g_cli.fps=f;}
		}else if(!strcmp(arg,"-d")) g_cli.debug_flags=atoi(value);
	}
	if(g_cli.reel_path[0]){
		bool output=g_cli.output_path[0]||g_cli.output_dir_set;
		if(g_cli.scene_path[0]||g_cli.batch||g_cli.layout||g_cli.list_cameras||g_cli.list_joints||g_cli.frames){fprintf(stderr,"--reel takes no scene or other mode\n");g_cli.invalid=true;}
		else if(g_cli.check==output){fprintf(stderr,"--reel needs --output FILE, --output-dir DIR or --check\n");g_cli.invalid=true;}
		else if(g_cli.output_path[0]&&g_cli.output_dir_set){fprintf(stderr,"--reel takes --output or --output-dir, not both\n");g_cli.invalid=true;}
		g_cli.screenshot_mode=!g_cli.check;
		return;
	}
	if(g_cli.check||g_cli.poster_path[0]){fprintf(stderr,"--check and --poster require --reel\n");g_cli.invalid=true;}
	if(g_cli.screenshot_mode&&!g_cli.scene_path[0]){fprintf(stderr,"rendering requires a scene\n");g_cli.invalid=true;}
	if(g_cli.list_cameras&&!g_cli.scene_path[0]){fprintf(stderr,"camera listing requires a scene\n");g_cli.invalid=true;}
	if(g_cli.list_joints&&(!g_cli.scene_path[0]||g_cli.screenshot_mode||g_cli.list_cameras)){fprintf(stderr,"joint listing requires a scene and no other mode\n");g_cli.invalid=true;}
	if(g_cli.frames&&!g_cli.batch){fprintf(stderr,"--frames requires --render\n");g_cli.invalid=true;}
	if(g_cli.batch&&g_cli.output_path[0]&&(!g_cli.frames||!g_cli.camera_name[0]||strcasecmp(strrchr(g_cli.output_path,'.')?strrchr(g_cli.output_path,'.'):"",".mp4"))){
		fprintf(stderr,"--render --output takes FILE.mp4 with --frames and --camera; images go to --output-dir\n");g_cli.invalid=true;
	}
	if((g_cli.layout&&g_cli.batch)||(g_cli.list_cameras&&g_cli.screenshot_mode)){fprintf(stderr,"select one CLI mode\n");g_cli.invalid=true;}
	if(g_cli.screenshot_mode&&!g_cli.batch&&!g_cli.layout&&!g_cli.output_path[0]) strcpy(g_cli.output_path,"screenshot.png");
}

static bool cli_make_dirs(const char *path) {
	char buf[1024];
	if(strlen(path)>=sizeof(buf)){fprintf(stderr,"output directory too long\n");return false;}
	strcpy(buf,path);
	for(char *p=buf+1;;p++){
		if(*p!='/'&&*p) continue;
		char saved=*p;*p=0;
		if(!axMkDir(buf)&&!axPathExists(buf)){fprintf(stderr,"cannot create %s: %s\n",buf,strerror(errno));return false;}
		*p=saved;if(!saved)break;
	}
	return true;
}

static bool cli_select_camera(Scene *scene,const char *name) {
	for(int i=0;i<scene->ncameras;i++) if(!strcmp(scene->cameras[i].name,name)){scene_select_camera(scene,name);return true;}
	fprintf(stderr,"unknown camera: %s\n",name);return false;
}

static bool cli_has_extension(const char *path,const char *ext){
	const char *dot=strrchr(path,'.');
	return dot&&!strcasecmp(dot,ext);
}

static bool cli_save_image(const char *path,const uint8_t *pixels,int w,int h){
	bool ok=cli_has_extension(path,".png")?save_image_png(path,pixels,w,h):
	        (cli_has_extension(path,".jpg")||cli_has_extension(path,".jpeg"))?save_image_jpg(path,pixels,w,h,CLI_JPEG_QUALITY):false;
	if(!ok)fprintf(stderr,"cannot write %s (use .png or .jpg)\n",path);
	return ok;
}

/* Renders a reel: a still (--time or .png/.jpg output), an MP4, or a PNG sequence. */
static bool cli_run_reel(void){
	reel_t *r=reel_load(g_cli.reel_path);
	if(!r)return false;
	bool ok=reel_sample(r,stderr);
	bool video=g_cli.output_path[0]&&cli_has_extension(g_cli.output_path,".mp4");
	if(ok&&g_cli.output_dir_set)ok=cli_make_dirs(g_cli.output_dir);
	ok=ok&&reel_gl_init(r,(g_cli.debug_flags&(DBG_NO_SHADOWS|DBG_WIREFRAME))|DBG_HIDE_CHARS|DBG_HIDE_LIGHTS|DBG_HIDE_GIZMOS);
	uint8_t *pixels=ok?malloc((size_t)r->width*r->height*4):NULL;
	ok=ok&&pixels;
	double start=axGetMilliseconds();
	int frames=0;
	if(ok&&g_cli.output_path[0]&&!video){
		ok=reel_gl_render(r,g_cli.time_set?g_cli.time:r->poster,pixels)&&cli_save_image(g_cli.output_path,pixels,r->width,r->height);
		frames=1;
	}else if(ok){
		reel_video_t *v=video?reel_video_open(g_cli.output_path,r->width,r->height,r->fps):NULL;
		ok=!video||v;
		for(int f=0;ok&&f<reel_frame_count(r);f++,frames++){
			ok=reel_gl_render(r,f/r->fps,pixels);
			if(ok&&v)ok=reel_video_write(v,pixels);
			else if(ok){
				char path[2048];snprintf(path,sizeof(path),"%s/frame_%04d.png",g_cli.output_dir,f);
				ok=cli_save_image(path,pixels,r->width,r->height);
			}
		}
		if(v&&!reel_video_close(v))ok=false;
	}
	double elapsed=axGetMilliseconds()-start;
	if(ok&&g_cli.poster_path[0])ok=reel_gl_render(r,r->poster,pixels)&&cli_save_image(g_cli.poster_path,pixels,r->width,r->height);
	if(ok)fprintf(stderr,"[reel] %s: %d frame%s %dx%d in %.2f s (%.1f ms/frame)%s%s\n",g_cli.reel_path,frames,frames==1?"":"s",
	              r->width,r->height,elapsed/1000,frames?elapsed/frames:0,g_cli.output_path[0]?" -> ":"",g_cli.output_path);
	free(pixels);
	reel_gl_free(r);reel_free(r);
	return ok;
}

static void create_app_windows(hinstance_t hinstance) {
#ifdef BUILD_AS_GEM
  g_app->menubar_win = set_app_menu(scener_menubar_proc, kMenus, kNumMenus,
                                    handle_menu_command, hinstance);
  create_main_toolbar_window();
#else
  g_app->chrome_win = create_application_chrome("SimpleSketch3D Chrome",
                                        scener_menubar_proc,
                                        kMenus, kNumMenus,
                                        scener_toolbar_proc,
                                        &scener_application_toolbar, hinstance);
  g_app->menubar_win      = app_chrome_menubar(g_app->chrome_win);
  g_app->main_toolbar_win = app_chrome_toolbar(g_app->chrome_win);
  scener_sync_main_toolbar();
#endif

  g_app->command_panel_win = create_command_panel_window();
  g_app->property_browser_win = create_property_browser_window();
}

static const char *scener_file_types[] = { ".blks", NULL };

#ifndef BUILD_AS_GEM
static bool scener_open_file_handler(const char *path) {
  return scener_open_file_path(path);
}
#endif

static uint8_t *cli_downsample(const uint8_t *pixels,int width,int height,int factor) {
	int outW=width/factor,outH=height/factor;
	uint8_t *out=malloc((size_t)outW*outH*4);if(!out)return NULL;
	float linear[256];for(int i=0;i<256;i++)linear[i]=powf(i/CLI_BYTE_MAX,CLI_COLOR_GAMMA);
	for(int y=0;y<outH;y++)for(int x=0;x<outW;x++){
		float sum[3]={0};
		for(int dy=0;dy<factor;dy++)for(int dx=0;dx<factor;dx++){
			const uint8_t *src=pixels+((size_t)(y*factor+dy)*width+x*factor+dx)*4;
			for(int c=0;c<3;c++)sum[c]+=linear[src[c]];
		}
		uint8_t *dst=out+((size_t)y*outW+x)*4;
		for(int c=0;c<3;c++)dst[c]=(uint8_t)fminf(CLI_BYTE_MAX,roundf(CLI_BYTE_MAX*powf(sum[c]/(factor*factor),1/CLI_COLOR_GAMMA)));
		dst[3]=255;
	}
	return out;
}

static bool scener_write_screenshot(scene_doc_t *doc, const char *path, reel_video_t *video) {
	if (!doc || !path || !path[0]) return false;
	int width = g_cli.width, height = g_cli.height;

	Scene *scene = &doc->scene;
	scene->camFov = scene->camFov > 0 ? scene->camFov : DEFAULT_FOV;
	if (g_cli.camera_name[0] && !g_cli.layout && !cli_select_camera(scene,g_cli.camera_name)) return false;

	vec3 dir = vsub(scene->camLook, scene->camPos);
	if (vlen(dir) < DIR_EPSILON) dir = v3(0, 0, -1);
	dir = vnorm(dir);
	mat4 proj = mat4_perspective(scene->camFov, (float)width / (float)height, SCENER_NEAR, SCENER_FAR);
	mat4 view = mat4_lookat(scene->camPos, scene->camLook, scene->worldUp);
	if(g_cli.layout){
		vec3 lo,hi;scene_get_bounds(scene,&lo,&hi);
		bool zup=scene->worldUp.z>0;float spanX=hi.x-lo.x,spanV=zup?hi.y-lo.y:hi.z-lo.z;
		if(!isfinite(spanX)||!isfinite(spanV)||spanX<=0||spanV<=0){fprintf(stderr,"empty layout bounds\n");return false;}
		width=(int)ceilf(spanX*100*g_cli.layout_scale*LAYOUT_PADDING);
		height=(int)ceilf(spanV*100*g_cli.layout_scale*LAYOUT_PADDING);
		if(width>CLI_MAX_SIZE||height>CLI_MAX_SIZE||width<=0||height<=0){fprintf(stderr,"layout dimensions exceed limit; reduce --scale\n");return false;}
		float minH=zup?lo.z:lo.y,maxH=zup?hi.z:hi.y;
		vec3 center=vscale(vadd(lo,hi),0.5f),eye=center;
		if(zup)eye.z=maxH+LAYOUT_EYE_OFFSET;else eye.y=maxH+LAYOUT_EYE_OFFSET;
		view=mat4_lookat(eye,center,zup?v3(0,1,0):v3(0,0,-1));
		float near_clip=LAYOUT_EYE_OFFSET+(maxH-minH)*(1-LAYOUT_CUT_FRACTION),far_clip=LAYOUT_EYE_OFFSET+maxH-minH+1;
		proj=mat4_identity();proj.m[0]=2/(spanX*LAYOUT_PADDING);proj.m[5]=2/(spanV*LAYOUT_PADDING);
		proj.m[10]=-2/(far_clip-near_clip);proj.m[14]=-(far_clip+near_clip)/(far_clip-near_clip);
		scene->camPos=eye;dir=vscale(scene->worldUp,-1);
	}

	int outW=width,outH=height;
	if(width>CLI_MAX_SIZE/g_cli.supersample||height>CLI_MAX_SIZE/g_cli.supersample){fprintf(stderr,"supersampled dimensions exceed %d; reduce --size, --scale or --supersample\n",CLI_MAX_SIZE);return false;}
	width*=g_cli.supersample;height*=g_cli.supersample;
	GLuint fbo = 0, color = 0, depth = 0;
	glGenFramebuffers(1, &fbo);
	glGenTextures(1, &color);
	glGenRenderbuffers(1, &depth);
	glBindTexture(GL_TEXTURE_2D, color);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glBindRenderbuffer(GL_RENDERBUFFER, depth);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depth);
	glDrawBuffer(GL_COLOR_ATTACHMENT0);
	if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE){fprintf(stderr,"screenshot framebuffer is incomplete\n");glBindFramebuffer(GL_FRAMEBUFFER,0);glDeleteFramebuffers(1,&fbo);glDeleteTextures(1,&color);glDeleteRenderbuffers(1,&depth);return false;}

	ui_begin_frame();
	glBindFramebuffer(GL_FRAMEBUFFER,fbo);
	glViewport(0, 0, width, height);
	glScissor(0, 0, width, height);
	glEnable(GL_SCISSOR_TEST);
	render_frame(scene, width, height, proj, view, scene->camPos, dir, g_cli.debug_flags|DBG_HIDE_GIZMOS|(g_cli.layout?DBG_FLAT:0));

	size_t bytes = (size_t)width * (size_t)height * 4;
	uint8_t *pixels = malloc(bytes);
	bool ok=pixels&&capture_framebuffer_rgba(width,height,pixels);
	if(ok&&g_cli.supersample>1){uint8_t *reduced=cli_downsample(pixels,width,height,g_cli.supersample);free(pixels);pixels=reduced;ok=pixels!=NULL;}
	width=outW;height=outH;
	const char *ext=strrchr(path,'.');
	if(ok&&video)ok=reel_video_write(video,pixels);
	else if(ok){
		if(ext&&(!strcasecmp(ext,".jpg")||!strcasecmp(ext,".jpeg")))ok=save_image_jpg(path,pixels,width,height,CLI_JPEG_QUALITY);
		else if(ext&&!strcasecmp(ext,".png"))ok=save_image_png(path,pixels,width,height);
		else{fprintf(stderr,"unsupported screenshot format: %s\n",path);ok=false;}
	}
	if(!ok)fprintf(stderr,"cannot write screenshot: %s\n",path);
	else if(!video)fprintf(stderr,"rendered %s (%dx%d)\n",path,width,height);
	free(pixels);

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glDeleteFramebuffers(1, &fbo);
	glDeleteTextures(1, &color);
	glDeleteRenderbuffers(1, &depth);
	return ok;
}

static bool cli_check_render_backend(void) {
	const char *vendor=(const char*)glGetString(GL_VENDOR);
	const char *renderer=(const char*)glGetString(GL_RENDERER);
	const char *version=(const char*)glGetString(GL_VERSION);
	fprintf(stderr,"[scener] OpenGL vendor=%s renderer=%s version=%s\n",vendor?vendor:"unknown",renderer?renderer:"unknown",version?version:"unknown");
	bool shadows=g_cli.screenshot_mode&&!g_cli.layout&&!(g_cli.debug_flags&(DBG_NO_SHADOWS|DBG_WIREFRAME));
	if(shadows&&renderer&&strstr(renderer,"Apple Software Renderer")){
		fprintf(stderr,"[scener] shadow export rejected: Apple Software Renderer produces invalid stencil shadows. Run Scener with GPU access (outside the restricted sandbox). Use -no-shadows only for diagnostic exports. No image was written.\n");
		return false;
	}
	return true;
}

bool gem_init(int argc, char *argv[], hinstance_t hinstance) {
  cli_parse(argc, argv);
  if(g_cli.invalid || !cli_check_render_backend()) return false;

  g_app = calloc(1, sizeof(app_state_t));
  if (!g_app) return false;

  g_app->hinstance   = hinstance;
  g_app->debug_flags  = g_cli.debug_flags;

#ifndef BUILD_AS_GEM
  ui_register_open_file_handler(scener_open_file_handler);
#endif

  srand((unsigned int)time(NULL));
  register_commctl_classes();
  if (g_cli.screenshot_mode) ui_begin_frame();
  shader_init();

  {
    char icons_path[4096];
    int n = snprintf(icons_path, sizeof(icons_path), "%s/../share/scener/icons",
                     ui_get_exe_dir());
    if (n > 0 && (size_t)n < sizeof(icons_path))
      bmp_add_icons_dir(icons_path);
  }

  if (g_cli.reel_path[0]) {
    bool ok = cli_run_reel();
    ui_request_quit();
    return ok;
  }

  if (!g_cli.screenshot_mode)
    create_app_windows(hinstance);

  g_app->accel = load_accelerators(scener_default_accels, scener_default_accel_count);
  g_app->navigation_accel = load_accelerators(kNavigationAccelEntries, kNavigationAccelCount);
  if (g_app->menubar_win)
    send_message(g_app->menubar_win, kMenuBarMessageSetAccelerators, 0, g_app->accel);

  scene_doc_t *doc = create_document_ex(g_cli.scene_path[0] ? g_cli.scene_path : NULL,
                                        !g_cli.screenshot_mode);
  if (!doc) return false;

  if(g_cli.camera_name[0]&&!cli_select_camera(&doc->scene,g_cli.camera_name))return false;
  if(g_cli.time!=0)scene_set_time(&doc->scene,g_cli.time);
  if(g_cli.screenshot_mode){
    if(g_cli.batch||g_cli.layout){
      if(!cli_make_dirs(g_cli.output_dir))return false;
      int count=g_cli.layout||g_cli.camera_name[0]?1:doc->scene.ncameras;
      char (*names)[MAX_CAMERA_NAME]=calloc((size_t)count,sizeof(*names));
      if(!names)return false;
      for(int i=0;i<count;i++)snprintf(names[i],sizeof(names[i]),"%s",g_cli.layout?"layout":g_cli.camera_name[0]?g_cli.camera_name:doc->scene.cameras[i].name);
      bool ok=true;
      for(int i=0;i<count&&ok;i++){
        if(strchr(names[i],'/')||strchr(names[i],'\\')||!names[i][0]){fprintf(stderr,"invalid output camera name: %s\n",names[i]);ok=false;break;}
        if(!g_cli.layout)ok=cli_select_camera(&doc->scene,names[i]);
        int frames=g_cli.frames?(int)floorf((g_cli.frame_end-g_cli.frame_start)*g_cli.fps+CLI_FRAME_EPSILON)+1:1;
        reel_video_t *video=NULL;
        if(g_cli.output_path[0]){
          video=reel_video_open(g_cli.output_path,g_cli.width,g_cli.height,g_cli.fps);
          ok=video!=NULL;
        }
        for(int f=0;f<frames&&ok;f++){
          char output[2048];
          if(g_cli.frames){
            scene_set_time(&doc->scene,g_cli.frame_start+f/g_cli.fps);
            snprintf(output,sizeof(output),"%s/%s_%04d.%s",g_cli.output_dir,names[i],f,g_cli.format);
          }else snprintf(output,sizeof(output),"%s/%s.%s",g_cli.output_dir,names[i],g_cli.format);
          ok=scener_write_screenshot(doc,video?g_cli.output_path:output,video);
        }
        if(video&&!reel_video_close(video))ok=false;
        if(video&&ok)fprintf(stderr,"rendered %s (%d frames %dx%d)\n",g_cli.output_path,frames,g_cli.width,g_cli.height);
      }
      free(names);if(!ok)return false;
    }else if(!scener_write_screenshot(doc,g_cli.output_path,NULL))return false;
    ui_request_quit();
  }

  return true;
}

void gem_shutdown(void) {
  if (!g_app) return;

  free_accelerators(g_app->accel);
  free_accelerators(g_app->navigation_accel);
  g_app->accel = NULL;
  g_app->navigation_accel = NULL;

  if (g_app->command_panel_win && is_window(g_app->command_panel_win))
    destroy_window(g_app->command_panel_win);
  g_app->command_panel_win = NULL;

  if (g_app->property_browser_win && is_window(g_app->property_browser_win))
    destroy_window(g_app->property_browser_win);
  g_app->property_browser_win = NULL;

  if (g_app->chrome_win && is_window(g_app->chrome_win))
    destroy_window(g_app->chrome_win);
  g_app->chrome_win = g_app->menubar_win = g_app->main_toolbar_win = NULL;

  while (g_app->docs)
    close_document(g_app->docs);

  render_deinit();
  shader_deinit();

  free(g_app);
  g_app = NULL;
}

GEM_DEFINE("SimpleSketch3D", SCENER_VERSION, gem_init, gem_shutdown, scener_file_types)

#ifndef BUILD_AS_GEM
int main(int argc, char *argv[]) {
  cli_parse(argc, argv);
  if(g_cli.invalid)return 2;
  if(g_cli.help){cli_usage();return 0;}
  if(g_cli.version){puts("scener " SCENER_VERSION);return 0;}
  if(g_cli.list_cameras){
    Scene scene={0};if(!load_scene(g_cli.scene_path,&scene))return 1;
    for(int i=0;i<scene.ncameras;i++)puts(scene.cameras[i].name);
    scene_free(&scene);return 0;
  }
  if(g_cli.reel_path[0]&&g_cli.check){
    reel_t *r=reel_load(g_cli.reel_path);
    bool ok=r&&reel_sample(r,stdout);
    reel_free(r);return ok?0:1;
  }
  if(g_cli.list_joints){
    Scene scene={0};if(!load_scene(g_cli.scene_path,&scene))return 1;
    if(g_cli.camera_name[0]&&!cli_select_camera(&scene,g_cli.camera_name)){scene_free(&scene);return 1;}
    scene_set_time(&scene,g_cli.time);
    scene_print_joints(&scene,stdout);
    scene_free(&scene);return 0;
  }
  int flags = g_cli.screenshot_mode ? UI_INIT_HIDDEN : UI_INIT_DESKTOP;
  int sample=g_cli.screenshot_mode?g_cli.supersample:1;
  if(g_cli.width>CLI_MAX_SIZE/sample||g_cli.height>CLI_MAX_SIZE/sample){fprintf(stderr,"supersampled dimensions exceed %d\n",CLI_MAX_SIZE);return 2;}
  // The platform drawable and screenshot target must share the working raster size.
  if (!ui_init_graphics(flags, "SimpleSketch3D", g_cli.width*sample, g_cli.height*sample)) return 1;
  if (!gem_init(argc, argv, 0)) {
    ui_shutdown_graphics();
    return 1;
  }
  if (!g_cli.screenshot_mode) {
    while (ui_is_running()) {
      ui_event_t e;
      while (get_message(&e)) {
        if (!translate_accelerator(g_app ? g_app->menubar_win : NULL, &e, scener_active_accelerators()))
          dispatch_message(&e);
      }
      repost_messages();
    }
  }
  gem_shutdown();
  ui_shutdown_graphics();
  return 0;
}
#endif
