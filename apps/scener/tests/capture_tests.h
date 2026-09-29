#ifndef __CAPTURE_TESTS_H__
#define __CAPTURE_TESTS_H__

static int capture_test_load_hands(Scene *s,const char *profile,const char *layer,const char *attributes){
	char path[]="apps/scener/scenes/.capture-test-XXXXXX";
	int fd=mkstemp(path); if(fd<0) return 0;
	FILE *file=fdopen(fd,"w"); if(!file){ close(fd); unlink(path); return 0; }
	fprintf(file,"<scene up='z'><sun dir='-1 -1 -1'/>%s<prefab name='Test' source='characters/presets/biped' %s>%s</prefab></scene>",profile,attributes,layer);
	fclose(file); int ok=load_scene(path,s); unlink(path); return ok;
}

static int capture_test_load(Scene *s,const char *profile,const char *layer){
	return capture_test_load_hands(s,profile,layer,"fingers='1'");
}

static mat4 capture_test_matrix(Scene *s,const char *name){
	for(int i=0;i<s->nrigJointWorlds;i++) if(!strcmp(scene_node_attr(s->rigJointWorlds[i].joint,"name"),name)) return s->rigJointWorlds[i].matrix;
	return mat4_identity();
}

static float capture_test_angle(mat4 a,mat4 b){
	mat4 d=mat4_mul(mat4_affine_inverse(a),b);
	return acosf(fmaxf(-1,fminf(1,(d.m[0]+d.m[5]+d.m[10]-1)*0.5f)))*180/M_PIf;
}

static void capture_test_add_channel(bvh_clip_t *clip,int joint,int channel,float value){
	bvh_joint_t *j=&clip->joints[joint];
	for(int f=1;f<clip->nframes;f++) for(int c=0;c<j->nchannels;c++) if(j->channels[c]==channel)
		clip->data[(size_t)f*clip->nchannels+j->firstChannel+c]+=value;
}

static void test_capture_hand_geometry(void){
	TEST("capture: preset knuckles are separated and mirrored");
	Scene s={0}; ASSERT_TRUE(load_scene("apps/scener/scenes/biped_study.blks",&s));
	const char *names[]={"index","middle","ring","pinky"};
	for(int i=0;i<4;i++){
		char name[32]; snprintf(name,sizeof(name),"left_%s",names[i]); vec3 left=cat_test_joint(&s,name);
		snprintf(name,sizeof(name),"right_%s",names[i]); vec3 right=cat_test_joint(&s,name);
		ASSERT_TRUE(fabsf(left.x+right.x)<0.01f && fabsf(left.y-right.y)<0.01f && fabsf(left.z-right.z)<0.01f);
		for(int j=i+1;j<4;j++){
			snprintf(name,sizeof(name),"left_%s",names[j]); ASSERT_TRUE(vlen(vsub(left,cat_test_joint(&s,name)))>1.8f);
		}
	}
	scene_free(&s); PASS();
}

static void test_capture_wrist_and_fingers(void){
	TEST("capture: axial wrist rotation and available finger channels survive retargeting");
	Scene s={0}; ASSERT_TRUE(capture_test_load(&s,"","<layer profile='cmu' mocap='mocap/cmu/02_01.bvh' contacts='none' legs='fk' hands='fk'/>"));
	MocapSource *m=&s.mocap[0];
	ASSERT_TRUE(m->digits[0][0][0]>=0 && m->digits[0][1][1]>=0 && m->digits[0][2][0]<0);
	scene_set_time(&s,0.6f); mat4 hand=capture_test_matrix(&s,"left_palm");
	capture_test_add_channel(m->clip,m->role[MOCAP_HAND],3,90);
	scene_set_time(&s,0.6f); mat4 turned=capture_test_matrix(&s,"left_palm");
	ASSERT_TRUE(fabsf(capture_test_angle(hand,turned)-90)<0.1f);
	mat4 index=capture_test_matrix(&s,"left_index_2");
	mat4 neutral=mat4_mul(mat4_affine_inverse(turned),capture_test_matrix(&s,"left_middle"));
	capture_test_add_channel(m->clip,m->digits[0][1][1],4,60);
	scene_set_time(&s,0.6f);
	ASSERT_TRUE(fabsf(capture_test_angle(index,capture_test_matrix(&s,"left_index_2"))-60)<0.1f);
	ASSERT_TRUE(capture_test_angle(turned,capture_test_matrix(&s,"left_palm"))<0.1f);
	mat4 after=mat4_mul(mat4_affine_inverse(capture_test_matrix(&s,"left_palm")),capture_test_matrix(&s,"left_middle"));
	ASSERT_TRUE(capture_test_angle(neutral,after)<0.1f);
	scene_free(&s); PASS();
}

static vec3 capture_test_pole(vec3 hip,vec3 knee,vec3 ankle){
	vec3 axis=vnorm(vsub(ankle,hip)),upper=vsub(knee,hip);
	return vnorm(vsub(upper,vscale(axis,vdot(upper,axis))));
}

static void test_capture_knee_poles(void){
	TEST("capture: knees follow the source after turning and inversion");
	Scene s={0}; ASSERT_TRUE(capture_test_load(&s,"","<layer profile='cmu' mocap='mocap/cmu/02_01.bvh' contacts='none'/>"));
	MocapSource *m=&s.mocap[0];
	capture_test_add_channel(m->clip,m->role[MOCAP_HIPS],4,180);
	capture_test_add_channel(m->clip,m->role[MOCAP_HIPS],3,90);
	for(int f=0;f<20;f++){
		scene_set_time(&s,0.2f+f*0.1f);
		for(int side=0;side<2;side++){
			vec3 h=m->pos[m->role[MOCAP_UPLEG+side]],k=m->pos[m->role[MOCAP_LEG+side]],a=m->pos[m->role[MOCAP_FOOT+side]];
			float bend=vdot(vnorm(vsub(k,h)),vnorm(vsub(a,k))); if(bend>cosf(10*M_PIf/180)) continue;
			vec3 source=mat4_xform_dir(m->toBody,capture_test_pole(h,k,a));
			vec3 target=capture_test_pole(cat_test_joint(&s,side?"right_thigh":"left_thigh"),cat_test_joint(&s,side?"right_calf":"left_calf"),cat_test_joint(&s,side?"right_foot":"left_foot"));
			ASSERT_TRUE(vdot(source,target)>cosf(1*M_PIf/180));
		}
	}
	scene_free(&s); PASS();
}

static void test_capture_contacts_and_scrubbing(void){
	TEST("capture: explicit planted foot survives pelvis motion, random scrubbing and save/reload");
	Scene s={0}; ASSERT_TRUE(capture_test_load(&s,"","<layer profile='cmu' mocap='mocap/cmu/02_01.bvh' contacts='none'><contact limb='left_leg' start='0.2' end='0.8' fade='0' mode='plant'/></layer>"));
	ASSERT_EQUAL(s.ignoredAttributes,0);
	scene_set_time(&s,0.3f); mat4 anchor=capture_test_matrix(&s,"left_foot");
	const float times[]={0.7f,0.4f,0.6f,0.3f};
	for(int i=0;i<4;i++){
		scene_set_time(&s,times[i]); mat4 actual=capture_test_matrix(&s,"left_foot");
		ASSERT_TRUE(vlen(vsub(mat4_xform_point(actual,v3(0,0,0)),mat4_xform_point(anchor,v3(0,0,0))))<0.001f);
		ASSERT_TRUE(capture_test_angle(anchor,actual)<0.1f);
		for(int k=0;k<s.nrigTargets;k++) ASSERT_TRUE(s.rigTargets[k].reachable);
	}
	for(int i=0;i<s.nprefabs;i++) snprintf(s.prefabs[i].path,sizeof(s.prefabs[i].path),"%s/scener-capture-preset-%d-%d.blk",window_test_temp_dir(),(int)getpid(),i);
	ASSERT_TRUE(scene_save_all(&s));
	for(int i=0;i<s.nprefabs;i++) unlink(s.prefabs[i].path);
	Scene restored={0}; ASSERT_TRUE(load_scene(s.scenePath,&restored));
	scene_set_time(&restored,0.3f); ASSERT_TRUE(vlen(vsub(cat_test_joint(&s,"left_foot"),cat_test_joint(&restored,"left_foot")))<0.01f);
	unlink(s.scenePath); scene_free(&restored); scene_free(&s); PASS();
}

static void test_capture_loop_and_profiles(void){
	TEST("capture: accumulated loops retain root travel and calibration profiles are explicit");
	Scene s={0}; ASSERT_TRUE(capture_test_load(&s,"<capture-profile name='Walk' referenceFrame='0' firstFrame='1' up='y'><map role='left_palm' target='left_palm' source='LeftHand' rotation='0 0 0'/></capture-profile>","<layer profile='Walk' mocap='mocap/cmu/02_01.bvh' loop='1' contacts='none'/>"));
	ASSERT_EQUAL(s.ignoredAttributes,0);
	ASSERT_EQUAL(s.mocap[0].referenceFrame,0); ASSERT_EQUAL(s.mocap[0].firstFrame,1);
	float length=bvh_duration(s.mocap[0].clip)-s.mocap[0].clip->frameTime;
	scene_set_time(&s,length-0.001f); vec3 before=cat_test_joint(&s,"pelvis");
	scene_set_time(&s,length+0.001f); vec3 after=cat_test_joint(&s,"pelvis");
	ASSERT_TRUE(vlen(vsub(after,before))<1);
	scene_set_time(&s,length*2+0.2f); ASSERT_TRUE(cat_test_joint(&s,"pelvis").y < -600);
	scene_free(&s);
	ASSERT_TRUE(capture_test_load(&s,"","<layer profile='bvh' mocap='mocap/cmu/02_01.bvh' contacts='none'/>"));
	ASSERT_EQUAL(s.mocap[0].referenceFrame,-1); ASSERT_EQUAL(s.mocap[0].firstFrame,0);
	bvh_clip_t *c=s.mocap[0].clip; vec3 *p=calloc((size_t)c->njoints,sizeof(*p)); quat *q=calloc((size_t)c->njoints,sizeof(*q));
	bvh_reference(c,-1,p,q);
	for(int i=0;i<c->njoints;i++) ASSERT_TRUE(fabsf(q[i].w-1)<0.001f);
	free(p); free(q); scene_free(&s); PASS();
}

static int capture_test_fixture(char *path,int zup){
	int fd=mkstemp(path); if(fd<0) return 0;
	FILE *f=fdopen(fd,"w"); if(!f){ close(fd); unlink(path); return 0; }
	fprintf(f,"HIERARCHY ROOT Hips { OFFSET 0 %d %d CHANNELS 6 Xposition Yposition Zposition Zrotation Yrotation Xrotation\n",zup?0:90,zup?90:0);
	for(int side=0;side<2;side++){
		const char *name=side?"Right":"Left";
		fprintf(f,"JOINT %sUpLeg { OFFSET %d 0 0 CHANNELS 3 Zrotation Yrotation Xrotation ",name,side?-10:10);
		fprintf(f,"JOINT %sLeg { OFFSET 0 %d %d CHANNELS 3 Zrotation Yrotation Xrotation ",name,zup?0:-40,zup?-40:0);
		fprintf(f,"JOINT %sFoot { OFFSET 0 %d %d CHANNELS 3 Zrotation Yrotation Xrotation End Site { OFFSET 0 %d %d } } } }\n",name,zup?0:-40,zup?-40:0,zup?-10:-5,zup?-5:10);
	}
	fputs("} MOTION Frames: 3 Frame Time: 0.1\n",f);
	for(int frame=0;frame<3;frame++) fprintf(f,"0 %d %d 0 0 0 0 0 %d 0 0 -60 0 0 30 0 0 %d 0 0 -60 0 0 30\n",zup?0:frame,zup?frame:0,30+frame*5,30+frame*5);
	fclose(f); return 1;
}

static void test_capture_axis_and_bent_reference(void){
	TEST("capture: Y/Z-up profiles agree, bent references use segment length, first frame is retained");
	Scene s[2]={0}; char paths[2][WINDOW_TEST_MAX_PATH],cwd[WINDOW_TEST_MAX_PATH];
	ASSERT_TRUE(getcwd(cwd,sizeof(cwd))!=NULL);
	const char *temp=window_test_temp_dir(); if(!strcmp(temp,".")) temp=cwd;
	for(int i=0;i<2;i++){
		snprintf(paths[i],sizeof(paths[i]),"%s/scener-capture-%d-XXXXXX",temp,i);
		ASSERT_TRUE(capture_test_fixture(paths[i],i));
		char profile[256],layer[WINDOW_TEST_MAX_PATH+256];
		snprintf(profile,sizeof(profile),"<capture-profile name='Test' up='%s' referenceFrame='0' firstFrame='0'/>",i?"z":"y");
		snprintf(layer,sizeof(layer),"<layer profile='Test' mocap='%s' contacts='none'/>",paths[i]);
		ASSERT_TRUE(capture_test_load(&s[i],profile,layer)); unlink(paths[i]);
		ASSERT_TRUE(s[i].mocap[0].valid); ASSERT_TRUE(fabsf(s[i].mocap[0].legLength-80)<0.001f);
		ASSERT_EQUAL(s[i].mocap[0].firstFrame,0);
		scene_set_time(&s[i],0.15f);
	}
	const char *joints[]={"pelvis","left_thigh","left_calf","left_foot","right_foot"};
	for(int i=0;i<5;i++) ASSERT_TRUE(vlen(vsub(cat_test_joint(&s[0],joints[i]),cat_test_joint(&s[1],joints[i])))<0.01f);
	scene_free(&s[0]); scene_free(&s[1]); PASS();
}

static void test_capture_hand_support(void){
	TEST("capture: a planted hand with a pose override remains fixed while scrubbing");
	Scene s={0};
	ASSERT_TRUE(capture_test_load(&s,"","<layer profile='cmu' mocap='mocap/cmu/02_01.bvh' contacts='none' legs='fk' handPose='HandsOpen'><contact limb='left_arm' start='0.2' end='0.8' fade='0' mode='plant'/></layer>"));
	scene_set_time(&s,0.3f); mat4 anchor=capture_test_matrix(&s,"left_palm");
	for(int i=0;i<4;i++){
		scene_set_time(&s,0.7f-i*0.1f); mat4 actual=capture_test_matrix(&s,"left_palm");
		ASSERT_TRUE(vlen(vsub(mat4_xform_point(actual,v3(0,0,0)),mat4_xform_point(anchor,v3(0,0,0))))<0.001f);
		ASSERT_TRUE(capture_test_angle(anchor,actual)<0.1f);
		for(int k=0;k<s.nrigTargets;k++) ASSERT_TRUE(s.rigTargets[k].reachable);
	}
	scene_free(&s); PASS();
}

static void test_capture_auto_contacts(void){
	TEST("capture: automatic supports scrub deterministically and remain reachable through release");
	Scene s={0}; ASSERT_TRUE(capture_test_load(&s,"","<layer profile='cmu' mocap='mocap/cmu/02_01.bvh'/>"));
	ASSERT_TRUE(s.mocap[0].ncontacts>=4);
	scene_set_time(&s,0.4f); mat4 before=capture_test_matrix(&s,"left_foot");
	for(int i=0;i<10;i++) scene_set_time(&s,2.5f-i*0.13f);
	scene_set_time(&s,0.4f); mat4 after=capture_test_matrix(&s,"left_foot");
	for(int i=0;i<16;i++) ASSERT_TRUE(fabsf(before.m[i]-after.m[i])<0.00001f);
	for(int frame=0;frame<=42;frame++){
		scene_set_time(&s,2.5f+frame/120.0f);
		for(int i=0;i<s.nrigTargets;i++) ASSERT_TRUE(s.rigTargets[i].reachable);
	}
	scene_free(&s); PASS();
}


static void test_capture_optional_fingers(void){
	TEST("capture: fingers default off, coexist with opted-in instances and persist on save");
	const char *motion="<layer profile='cmu' mocap='mocap/cmu/02_01.bvh' contacts='none'/>";
	char detailed[512]; snprintf(detailed,sizeof(detailed),"<prefab name='Detailed' source='characters/presets/biped' fingers='1'>%s</prefab>",motion);
	Scene s={0}; ASSERT_TRUE(capture_test_load_hands(&s,detailed,motion,""));
	ASSERT_EQUAL(s.ignoredAttributes,0); ASSERT_EQUAL(s.nmocap,1);
	for(int frame=0;frame<3;frame++){
		scene_set_time(&s,frame*0.3f);
		void *simple=NULL,*full=NULL; int fingerJoints[2]={0,0},toes[2]={0,0}; mat4 palms[2];
		for(int i=0;i<s.nrigJointWorlds;i++){
			RigJointWorld *j=&s.rigJointWorlds[i]; const char *name=scene_node_attr(j->joint,"name");
			int detailedInstance=!strcmp(scene_node_attr(j->instance,"name"),"Detailed");
			if(detailedInstance) full=j->instance; else simple=j->instance;
			if(strstr(name,"thumb") || strstr(name,"index") || strstr(name,"middle") || strstr(name,"ring") || strstr(name,"pinky")) fingerJoints[detailedInstance]++;
			if(strstr(name,"toe")) toes[detailedInstance]++;
			if(!strcmp(name,"left_palm")) palms[detailedInstance]=j->matrix;
		}
		ASSERT_TRUE(simple && full); ASSERT_EQUAL(fingerJoints[0],0); ASSERT_EQUAL(fingerJoints[1],30);
		ASSERT_EQUAL(toes[0],4); ASSERT_EQUAL(toes[1],4);
		ASSERT_EQUAL(scene_rig_joint_count(&s,full)-scene_rig_joint_count(&s,simple),30);
		for(int i=0;i<16;i++) ASSERT_TRUE(fabsf(palms[0].m[i]-palms[1].m[i])<0.00001f);
		for(int i=0;i<scene_rig_joint_count(&s,simple);i++){
			void *joint=scene_rig_joint_at(&s,simple,i,NULL); ASSERT_TRUE(joint!=NULL);
			ASSERT_TRUE(strcmp(scene_node_tag(joint),"digit") || strstr(scene_node_attr(joint,"name"),"toe"));
		}
	}
	for(int i=0;i<s.nprefabs;i++) snprintf(s.prefabs[i].path,sizeof(s.prefabs[i].path),"%s/scener-fingers-preset-%d-%d.blk",window_test_temp_dir(),(int)getpid(),i);
	ASSERT_TRUE(scene_save_all(&s)); for(int i=0;i<s.nprefabs;i++) unlink(s.prefabs[i].path);
	Scene restored={0}; ASSERT_TRUE(load_scene(s.scenePath,&restored)); scene_set_time(&restored,0.6f);
	ASSERT_EQUAL(restored.nrigJointWorlds,s.nrigJointWorlds); ASSERT_EQUAL(restored.nobjs,s.nobjs);
	ASSERT_EQUAL(restored.ignoredAttributes,0);
	unlink(s.scenePath); scene_free(&restored); scene_free(&s); PASS();
}

static void test_prefab_finger_defaults(void){
	TEST("prefab: finger option also works in direct prefab editing and leaves toes enabled");
	Scene s={0};
	const char *body="<palm name='hand' ground='0' length='9'><digit name='index' length='4' radius='1'/></palm>"
		"<ankle name='foot' ground='0' length='15'><digit name='toe' length='4' radius='1'/></ankle>";
	char xml[512]; snprintf(xml,sizeof(xml),"<prefab>%s</prefab>",body);
	ASSERT_TRUE(window_test_load(&s,xml)); ASSERT_EQUAL(s.nobjs,3); ASSERT_EQUAL(s.ignoredAttributes,0); scene_free(&s);
	snprintf(xml,sizeof(xml),"<prefab fingers='1'>%s</prefab>",body);
	ASSERT_TRUE(window_test_load(&s,xml)); ASSERT_EQUAL(s.nobjs,4); ASSERT_EQUAL(s.ignoredAttributes,0); scene_free(&s); PASS();
}


static void test_segmented_joint_only_volumes(void){
	TEST("bone volumes: generated spine links inherit volume=0 and retain attached shapes");
	Scene s={0}; ASSERT_TRUE(window_test_load(&s,"<scene up='z'><sun dir='-1 -1 -1'/>"
		"<spine name='spine' links='3' aim='0 90' length='24' radius='13 9.5' volume='0'>"
		"<ellipsoid on='0 0' at='0.5' radii='13 9 20' sink='9.5'/></spine></scene>"));
	ASSERT_EQUAL(s.nobjs,1); ASSERT_EQUAL(s.ignoredAttributes,0);
	ASSERT_TRUE(!strcmp(scene_node_tag(s.objs[0].editNode),"spine"));
	scene_free(&s); PASS();
}
static void test_character_cosmetic_options(void){
  TEST("character options: cosmetic variants retain joints, inherit defaults and persist per instance");
  char path[512],xml[2048];
  snprintf(path,sizeof(path),"%s/scener-options-%d.blk",window_test_temp_dir(),getpid());
  FILE *file=fopen(path,"w"); ASSERT_TRUE(file!=NULL);
  fputs("<prefab><option name='gloves' enabled='1'/><palm name='hand' ground='0' volume='0'>"
        "<ellipsoid radii='3 4 5' color='1 0 0' if-feature='gloves'/>"
        "<ellipsoid radii='2 3 4' color='0 1 0' unless-feature='gloves'/>"
        "<ellipsoid radii='4 5 2' pos='0 0 4' if-feature='gloves'/></palm>"
        "<clip name='Turn' length='1'><key t='0'><joint target='hand'/></key><key t='1'><joint target='hand' rot='0 0 45'/></key></clip></prefab>",file);
  fclose(file);
  Scene direct={0}; ASSERT_TRUE(load_scene(path,&direct));
  ASSERT_EQUAL(direct.nobjs,2); ASSERT_EQUAL(direct.ignoredAttributes,0); scene_free(&direct);
  const char *base=strrchr(path,'/'); base=base?base+1:path;
  char ref[512]; snprintf(ref,sizeof(ref),"../%.*s",(int)strlen(base)-4,base);
  /* load_prefab uses assetRoot/prefabs/ref.blk; an existing prefabs directory is needed. */
  char folder[512]; snprintf(folder,sizeof(folder),"%s/prefabs",window_test_temp_dir());
  ASSERT_TRUE(axMkDir(folder));
  snprintf(xml,sizeof(xml),"<scene><sun dir='-1 -1 -1'/><prefab name='Gloved' source='%s'><layer clip='Turn'/></prefab>"
           "<prefab name='Bare' source='%s'><option name='gloves' enabled='0'/><layer clip='Turn'/></prefab></scene>",ref,ref);
  Scene s={0}; ASSERT_TRUE(window_test_load(&s,xml));
  ASSERT_EQUAL(s.nobjs,3); ASSERT_EQUAL(s.ignoredAttributes,0); ASSERT_EQUAL(s.nrigJointWorlds,2);
  scene_set_time(&s,.5f); ASSERT_EQUAL(s.nobjs,3); ASSERT_EQUAL(s.nrigJointWorlds,2);
  for(int i=0;i<16;i++) ASSERT_TRUE(fabsf(s.rigJointWorlds[0].matrix.m[i]-s.rigJointWorlds[1].matrix.m[i])<.00001f);
  ASSERT_TRUE(scene_save_all(&s)); Scene restored={0}; ASSERT_TRUE(load_scene(s.scenePath,&restored));
  ASSERT_EQUAL(restored.nobjs,3); ASSERT_EQUAL(restored.ignoredAttributes,0);
  unlink(s.scenePath); unlink(path); scene_free(&restored); scene_free(&s); PASS();
}

#endif

static int volume_meshes_match(Scene *a,Scene *b){
  if(a->nobjs!=b->nobjs) return 0;
  for(int i=0;i<a->nobjs;i++){
    Mesh *m=&a->objs[i].mesh; int found=0;
    for(int j=0;j<b->nobjs && !found;j++){
      Mesh *n=&b->objs[j].mesh;
      if(m->nverts!=n->nverts || m->ntris!=n->ntris) continue;
      float error=0;
      for(int k=0;k<m->nverts;k++) error=fmaxf(error,vlen(vsub(m->verts[k].pos,n->verts[k].pos)));
      found=error<0.0002f;
    }
    if(!found){ fprintf(stderr,"[scener-test] unmatched source volume %d\n",i); return 0; }
  }
  return 1;
}

static void test_preserved_cat_geometry(void){
  TEST("calibrated CAT: preserves source volumes and imported motion; supports IK, torso bends and gait");
  const char *base="apps/scener/imports/examples/ecstatica2/scenes/";
  char path[512]; Scene source={0},cat={0};
  snprintf(path,sizeof(path),"%spreview.blks",base); ASSERT_TRUE(load_scene(path,&source));
  snprintf(path,sizeof(path),"%scontrolled.blks",base); ASSERT_TRUE(load_scene(path,&cat));
  ASSERT_EQUAL(cat.ignoredAttributes,0); ASSERT_TRUE(volume_meshes_match(&source,&cat));
  vec3 foot=cat_test_joint(&cat,"left_foot"),hand=cat_test_joint(&cat,"left_palm");
  scene_free(&source); scene_free(&cat);
  snprintf(path,sizeof(path),"%scontrolled-presentleft.blks",base); ASSERT_TRUE(load_scene(path,&cat));
  ASSERT_EQUAL(cat.nrigTargets,1); ASSERT_TRUE(cat.rigTargets[0].reachable);
  ASSERT_TRUE(vlen(vsub(foot,cat_test_joint(&cat,"left_foot")))<.01f);
  ASSERT_TRUE(vlen(vsub(hand,cat_test_joint(&cat,"left_palm")))>10);
  scene_free(&cat);
  snprintf(path,sizeof(path),"%sanimations/0008-stherorun.blks",base); ASSERT_TRUE(load_scene(path,&source));
  snprintf(path,sizeof(path),"%sanimations/controlled-run.blks",base); ASSERT_TRUE(load_scene(path,&cat));
  for(int i=0;i<4;i++){
    scene_set_time(&source,i*.27f); scene_set_time(&cat,i*.27f);
    ASSERT_TRUE(volume_meshes_match(&source,&cat));
  }
  scene_free(&source); scene_free(&cat);
  snprintf(path,sizeof(path),"%scontrolled-walk.blks",base); ASSERT_TRUE(load_scene(path,&cat));
  scene_set_time(&cat,.7f); ASSERT_EQUAL(cat.nrigTargets,2);
  for(int i=0;i<cat.nrigTargets;i++) ASSERT_TRUE(cat.rigTargets[i].reachable);
  ASSERT_TRUE(vlen(vsub(foot,cat_test_joint(&cat,"left_foot")))>1);
  scene_free(&cat); PASS();
}

static void test_volume_character_cast(void){
  TEST("source morphs: defaults preserve all source vertices and cast references cache once");
  const char *bases[]={"joe","villager-full","villager-belly","villager-slim","freegirl"};
  for(int i=0;i<5;i++){
    char path[512]; Scene source={0},morph={0};
    snprintf(path,sizeof(path),"apps/scener/characters/%s/scenes/source.blks",bases[i]);
    ASSERT_TRUE(load_scene(path,&source));
    snprintf(path,sizeof(path),"apps/scener/characters/%s/scenes/study.blks",bases[i]);
    ASSERT_TRUE(load_scene(path,&morph)); ASSERT_EQUAL(morph.ignoredAttributes,0);
    ASSERT_TRUE(volume_meshes_match(&source,&morph));
    scene_free(&source); scene_free(&morph);
  }
  Scene s={0}; ASSERT_TRUE(load_scene("apps/scener/scenes/volume_character_lineup.blks",&s));
  ASSERT_EQUAL(s.nprefabs,4); ASSERT_EQUAL(s.ninstances,4); ASSERT_EQUAL(s.ignoredAttributes,0);
  int count=s.nobjs;
  scene_set_time(&s,1); ASSERT_EQUAL(s.nprefabs,4); ASSERT_EQUAL(s.ninstances,4); ASSERT_EQUAL(s.nobjs,count);
  scene_free(&s); PASS();
}
