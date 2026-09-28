#ifndef __CAPTURE_TESTS_H__
#define __CAPTURE_TESTS_H__

static int capture_test_load(Scene *s,const char *profile,const char *layer){
	char path[]="apps/scener/scenes/.capture-test-XXXXXX";
	int fd=mkstemp(path); if(fd<0) return 0;
	FILE *file=fdopen(fd,"w"); if(!file){ close(fd); unlink(path); return 0; }
	fprintf(file,"<scene up='z'><sun dir='-1 -1 -1'/>%s<prefab name='Test' source='characters/presets/biped'>%s</prefab></scene>",profile,layer);
	fclose(file); int ok=load_scene(path,s); unlink(path); return ok;
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
	for(int i=0;i<s.nprefabs;i++) snprintf(s.prefabs[i].path,sizeof(s.prefabs[i].path),"/tmp/scener-capture-preset-%d-%d.blk",(int)getpid(),i);
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
	Scene s[2]={{0},{0}}; char paths[2][64]={"/tmp/scener-capture-y-XXXXXX","/tmp/scener-capture-z-XXXXXX"};
	for(int i=0;i<2;i++){
		ASSERT_TRUE(capture_test_fixture(paths[i],i));
		char profile[256],layer[256];
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
	TEST("capture: automatic supports are cached and sampling order does not change the pose");
	Scene s={0}; ASSERT_TRUE(capture_test_load(&s,"","<layer profile='cmu' mocap='mocap/cmu/02_01.bvh'/>"));
	ASSERT_TRUE(s.mocap[0].ncontacts>=4);
	scene_set_time(&s,0.4f); mat4 before=capture_test_matrix(&s,"left_foot");
	for(int i=0;i<10;i++) scene_set_time(&s,2.5f-i*0.13f);
	scene_set_time(&s,0.4f); mat4 after=capture_test_matrix(&s,"left_foot");
	for(int i=0;i<16;i++) ASSERT_TRUE(fabsf(before.m[i]-after.m[i])<0.00001f);
	scene_free(&s); PASS();
}

#endif
