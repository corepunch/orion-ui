#ifndef __SCENE_CAPTURE_H__
#define __SCENE_CAPTURE_H__

#define CAPTURE_FLOOR_QUANTILE 0.05f
#define CAPTURE_CONTACT_HEIGHT 0.08f
#define CAPTURE_CONTACT_SPEED 0.35f
#define CAPTURE_CONTACT_FADE 0.05f
#define CAPTURE_CONTACT_MIN_TIME 0.06f
#define CAPTURE_CONTACT_HYSTERESIS 1.5f
#define CAPTURE_POLE_EPSILON 0.001f
#define CAPTURE_REACH_ITERATIONS 12
#define CAPTURE_REACH_MARGIN 0.001f
#define CAPTURE_MAX_CYCLES 1000000
#define CAPTURE_LOOP_BLEND 0.12f


static void mocap_free_all(Scene *s){
	for(int i=0;i<s->nmocap;i++){
		MocapSource *m=&s->mocap[i];
		bvh_free(m->clip); free(m->refPos); free(m->pos); free(m->refRot); free(m->rot); free(m->contacts); xml_free(m->profile);
	}
	free(s->mocap); s->mocap=NULL; s->nmocap=s->cmocap=0;
}

static void mocap_normalize(const char *name,char *out,size_t size){
	const char *colon=strrchr(name,':');
	if(colon) name=colon+1;
	size_t n=0;
	for(;*name && n+1<size;name++) if(isalnum((unsigned char)*name)) out[n++]=(char)tolower((unsigned char)*name);
	out[n]=0;
	if(!strncmp(out,"bip01",5)) memmove(out,out+5,strlen(out+5)+1);
}

/* Source joint for a role on one side ("left"/"l" or "right"/"r"), by common names. */
static int mocap_find_side(const bvh_clip_t *c,const char *const *names,int right){
	char want[MOCAP_NAME_CAPACITY],have[MOCAP_NAME_CAPACITY];
	for(int k=0;names[k];k++) for(int prefix=0;prefix<2;prefix++){
		snprintf(want,sizeof(want),"%s%s",prefix?(right?"r":"l"):(right?"right":"left"),names[k]);
		for(int i=0;i<c->njoints;i++){ mocap_normalize(c->joints[i].name,have,sizeof(have)); if(!strcmp(have,want)) return i; }
	}
	return -1;
}

static int mocap_find(const bvh_clip_t *c,const char *const *names){
	char have[MOCAP_NAME_CAPACITY];
	for(int k=0;names[k];k++) for(int i=0;i<c->njoints;i++){
		mocap_normalize(c->joints[i].name,have,sizeof(have));
		if(!strcmp(have,names[k])) return i;
	}
	return -1;
}

static int mocap_first_child(const bvh_clip_t *c,int joint){
	for(int i=0;joint>=0 && i<c->njoints;i++) if(c->joints[i].parent==joint) return i;
	return -1;
}

/* Joints from `from` (exclusive) down to `to` (inclusive), in order. */
static int mocap_chain(const bvh_clip_t *c,int from,int to,int *out){
	int n=0,walk=to;
	int reversed[MOCAP_MAX_CHAIN];
	while(walk>=0 && walk!=from && n<MOCAP_MAX_CHAIN){ reversed[n++]=walk; walk=c->joints[walk].parent; }
	if(walk!=from) return 0;
	for(int i=0;i<n;i++) out[i]=reversed[n-1-i];
	return n;
}

static const char *capture_roles[MOCAP_ROLES]={
	"hips","chest","head","head_tip","left_thigh","right_thigh","left_calf","right_calf",
	"left_foot","right_foot","left_toe","right_toe","left_collarbone","right_collarbone",
	"left_upperarm","right_upperarm","left_forearm","right_forearm","left_palm","right_palm","left_palm_tip","right_palm_tip"
};
static const char *capture_digits[MOCAP_DIGITS]={"thumb","index","middle","ring","pinky"};

static int capture_named(const bvh_clip_t *c,const char *name){
	char want[MOCAP_NAME_CAPACITY],have[MOCAP_NAME_CAPACITY];
	mocap_normalize(name,want,sizeof(want));
	int found=-1;
	for(int i=0;i<c->njoints;i++){
		mocap_normalize(c->joints[i].name,have,sizeof(have));
		if(strcmp(want,have)) continue;
		if(found>=0){ fprintf(stderr,"[mocap] ambiguous source joint '%s'; use an exact name\n",name); fflush(stderr); return bvh_find(c,name); }
		found=i;
	}
	return found;
}

static int capture_absolute_path(const char *path){
	return path[0]=='/' || path[0]=='\\' || (isalpha((unsigned char)path[0]) && path[1]==':' && (path[2]=='/' || path[2]=='\\'));
}

static XmlNode *capture_profile(Scene *s,XmlNode *proot,const char *name){
	XmlNode *roots[]={(XmlNode*)s->sceneRoot,proot};
	for(int r=0;r<2;r++) for(int i=0;roots[r] && i<roots[r]->nkids;i++){
		XmlNode *n=roots[r]->kids[i];
		if(!strcmp(n->tag,"capture-profile") && !strcmp(xml_attr(n,"name",""),name)) return xml_clone(n,NULL);
	}
	if(!strcmp(name,"bvh") || !strcmp(name,"cmu")){
		XmlNode *n=xml_new("capture-profile");
		xml_set_attr(n,"name",name);
		if(!strcmp(name,"cmu")){ xml_set_attr(n,"referenceFrame","0"); xml_set_attr(n,"firstFrame","1"); }
		return n;
	}
	char path[MOCAP_PATH_CAPACITY];
	snprintf(path,sizeof(path),"%s%s%s",capture_absolute_path(name)?"":s->assetRoot,capture_absolute_path(name)||!s->assetRoot[0]?"":"/",name);
	char *text=read_file(path); XmlNode *n=text?xml_parse(text):NULL; free(text);
	if(n && !strcmp(n->tag,"capture-profile")) return n;
	xml_free(n); fprintf(stderr,"[mocap] unknown capture profile '%s'\n",name); fflush(stderr); return NULL;
}

static vec3 capture_axis(const char *axis){
	static const char *names[]={"x","y","z","-x","-y","-z"};
	static const vec3 vectors[]={{1,0,0},{0,1,0},{0,0,1},{-1,0,0},{0,-1,0},{0,0,-1}};
	for(int i=0;i<6;i++) if(!strcmp(axis,names[i])) return vectors[i];
	fprintf(stderr,"[mocap] invalid up axis '%s'\n",axis); fflush(stderr); return v3(0,0,0);
}

static void mocap_map(MocapSource *m){
	static const char *const hips[]={"hips","hip","pelvis","root",NULL},*head[]={"head",NULL};
	static const char *const upleg[]={"upleg","thigh","upperleg","hip",NULL},*leg[]={"leg","calf","shin","lowerleg","knee",NULL};
	static const char *const foot[]={"foot","ankle",NULL},*toe[]={"toebase","toe","toes","ball",NULL};
	static const char *const collar[]={"shoulder","collar","clavicle",NULL},*arm[]={"arm","upperarm","uparm",NULL};
	static const char *const forearm[]={"forearm","lowerarm","elbow",NULL},*hand[]={"hand","wrist",NULL};
	bvh_clip_t *c=m->clip;
	m->role[MOCAP_HIPS]=mocap_find(c,hips); m->role[MOCAP_HEAD]=mocap_find(c,head);
	m->role[MOCAP_HEAD_END]=mocap_first_child(c,m->role[MOCAP_HEAD]);
	for(int side=0;side<2;side++){
		m->role[MOCAP_UPLEG+side]=mocap_find_side(c,upleg,side); m->role[MOCAP_LEG+side]=mocap_find_side(c,leg,side);
		m->role[MOCAP_FOOT+side]=mocap_find_side(c,foot,side); m->role[MOCAP_TOE+side]=mocap_find_side(c,toe,side);
		m->role[MOCAP_COLLAR+side]=mocap_find_side(c,collar,side); m->role[MOCAP_ARM+side]=mocap_find_side(c,arm,side);
		m->role[MOCAP_FOREARM+side]=mocap_find_side(c,forearm,side); m->role[MOCAP_HAND+side]=mocap_find_side(c,hand,side);
		static const char *const palmTip[]={"handmiddle1","middle1","fingerbase","handindex1","index1","handend",NULL};
		m->role[MOCAP_HAND_END+side]=mocap_find_side(c,palmTip,side);
		if(m->role[MOCAP_HAND_END+side]<0){
			int handJoint=m->role[MOCAP_HAND+side],child=mocap_first_child(c,handJoint);
			if(child>=0 && !c->joints[child].nchannels) m->role[MOCAP_HAND_END+side]=child;
		}
		for(int digit=0;digit<MOCAP_DIGITS;digit++) for(int link=0;link<MOCAP_KNUCKLES;link++){
			char a[MOCAP_NAME_CAPACITY],b[MOCAP_NAME_CAPACITY];
			snprintf(a,sizeof(a),"hand%s%d",capture_digits[digit],link+1);
			snprintf(b,sizeof(b),"%s%d",capture_digits[digit],link+1);
			const char *names[]={a,b,NULL};
			m->digits[side][digit][link]=mocap_find_side(c,names,side);
		}
		static const char *const thumb[]={"thumb",NULL},*base[]={"fingerbase",NULL};
		int baseJoint=mocap_find_side(c,base,side);
		if(baseJoint>=0){
			m->digits[side][1][1]=m->digits[side][1][0];
			m->digits[side][1][0]=baseJoint;
		}
		if(m->digits[side][0][0]<0) m->digits[side][0][0]=mocap_find_side(c,thumb,side);
		if(m->role[MOCAP_TOE+side]<0) m->role[MOCAP_TOE+side]=mocap_first_child(c,m->role[MOCAP_FOOT+side]);
	}
	XmlNode *profile=m->profile;
	for(int k=0;profile && k<profile->nkids;k++){
		XmlNode *map=profile->kids[k];
		if(strcmp(map->tag,"map")) continue;
		const char *role=xml_attr(map,"role",NULL),*source=xml_attr(map,"source","");
		int index=capture_named(c,source);
		if(index<0){ fprintf(stderr,"[mocap] mapping source '%s' is absent\n",source); fflush(stderr); m->valid=0; }
		if(!role) continue;
		int found=0;
		for(int i=0;i<MOCAP_ROLES;i++) if(!strcmp(role,capture_roles[i])){ m->role[i]=index; found=1; }
		if(!found){ fprintf(stderr,"[mocap] unknown mapping role '%s'\n",role); fflush(stderr); m->valid=0; }
	}
	/* The chest is where the arms branch off the spine. */
	int shoulder=m->role[MOCAP_COLLAR]>=0?m->role[MOCAP_COLLAR]:m->role[MOCAP_ARM];
	if(m->role[MOCAP_CHEST]<0) m->role[MOCAP_CHEST]=shoulder>=0?c->joints[shoulder].parent:-1;
	m->nspine=m->role[MOCAP_HIPS]>=0 && m->role[MOCAP_CHEST]>=0?mocap_chain(c,m->role[MOCAP_HIPS],m->role[MOCAP_CHEST],m->spine):0;
	m->nneck=m->role[MOCAP_CHEST]>=0 && m->role[MOCAP_HEAD]>=0?mocap_chain(c,m->role[MOCAP_CHEST],m->role[MOCAP_HEAD],m->neck):0;
}

static int capture_float_compare(const void *a,const void *b){
	float x=*(const float*)a,y=*(const float*)b;
	return (x>y)-(x<y);
}

static int capture_effector(const MocapSource *m,int i){ return m->role[(i<2?MOCAP_FOOT:MOCAP_HAND)+(i%2)]; }
static int capture_hip(const MocapSource *m,int i){ return m->role[(i<2?MOCAP_UPLEG:MOCAP_ARM)+(i%2)]; }

static void capture_contacts(MocapSource *m){
	bvh_clip_t *c=m->clip;
	size_t count=(size_t)c->nframes*c->njoints;
	vec3 *points=malloc(count*sizeof(*points)); quat *turns=malloc((size_t)c->njoints*sizeof(*turns));
	float *heights=malloc((size_t)c->nframes*2*sizeof(*heights)); int nheights=0;
	if(!points || !turns || !heights){
		fprintf(stderr,"[mocap] cannot allocate contact samples for %s\n",m->path); fflush(stderr);
		free(points); free(turns); free(heights); m->valid=0; return;
	}
	for(int f=0;f<c->nframes;f++){
		bvh_evaluate(c,f*c->frameTime,points+(size_t)f*c->njoints,turns);
		if(f<m->firstFrame) continue;
		for(int i=0;i<2;i++) heights[nheights++]=mat4_xform_dir(m->toBody,points[(size_t)f*c->njoints+capture_effector(m,i)]).z;
	}
	qsort(heights,(size_t)nheights,sizeof(*heights),capture_float_compare);
	m->floor=xml_attr_f(m->profile,"floor",heights[(int)((nheights-1)*CAPTURE_FLOOR_QUANTILE)]);
	float refFloor=INFINITY;
	for(int i=0;i<2;i++) refFloor=fminf(refFloor,mat4_xform_dir(m->toBody,m->refPos[capture_effector(m,i)]).z);
	m->groundShift=refFloor-m->floor;
	if(!isfinite(m->floor)){ fprintf(stderr,"[mocap] nonfinite floor in profile %s\n",m->profileName); fflush(stderr); m->valid=0; free(points); free(turns); free(heights); return; }
	for(int e=0;e<4;e++){
		int joint=capture_effector(m,e),hip=capture_hip(m,e),begin=-1;
		if(joint<0 || hip<0) continue;
		for(int f=m->firstFrame;f<=c->nframes;f++){
			int low=0;
			if(f<c->nframes){
				vec3 p=points[(size_t)f*c->njoints+joint];
				int before=f>m->firstFrame?f-1:f,after=f+1<c->nframes?f+1:f;
				float dt=(after-before)*c->frameTime;
				float speed=dt>0?vlen(vsub(points[(size_t)after*c->njoints+joint],points[(size_t)before*c->njoints+joint]))/dt:0;
				float hysteresis=begin<0?1:CAPTURE_CONTACT_HYSTERESIS;
				float height=mat4_xform_dir(m->toBody,p).z-m->floor;
				low=speed<=m->contactSpeed*m->legLength*hysteresis && height<=m->contactHeight*m->legLength*hysteresis;
			}
			if(low && begin<0) begin=f;
			if(low || begin<0) continue;
			if((f-begin)*c->frameTime>=CAPTURE_CONTACT_MIN_TIME){
				MocapContact contact={e,begin,f-1};
				DA_PUSH(m->contacts,m->ncontacts,m->ccontacts,contact);
			}
			begin=-1;
		}
	}
	free(points); free(turns); free(heights);
}

static MocapSource *mocap_source(Scene *s,XmlNode *proot,XmlNode *layer){
	const char *file=xml_attr(layer,"mocap",""),*profileName=xml_attr(layer,"profile","bvh");
	char path[MOCAP_PATH_CAPACITY];
	snprintf(path,sizeof(path),"%s%s%s",capture_absolute_path(file)?"":s->assetRoot,capture_absolute_path(file)||!s->assetRoot[0]?"":"/",file);
	for(int i=0;i<s->nmocap;i++) if(!strcmp(s->mocap[i].path,path) && !strcmp(s->mocap[i].profileName,profileName) && s->mocap[i].rigDefinition==proot) return s->mocap[i].valid?&s->mocap[i]:NULL;
	MocapSource m; memset(&m,0,sizeof(m)); m.rigDefinition=proot;
	memset(m.digits,-1,sizeof(m.digits));
	snprintf(m.path,sizeof(m.path),"%s",path); snprintf(m.profileName,sizeof(m.profileName),"%s",profileName);
	m.profile=capture_profile(s,proot,profileName); m.clip=m.profile?bvh_load(path):NULL; m.valid=m.clip!=NULL;
	if(m.clip){
		int n=m.clip->njoints;
		for(int i=0;i<MOCAP_ROLES;i++) m.role[i]=-1;
		m.refPos=malloc(sizeof(vec3)*(size_t)n); m.pos=malloc(sizeof(vec3)*(size_t)n);
		m.refRot=malloc(sizeof(quat)*(size_t)n); m.rot=malloc(sizeof(quat)*(size_t)n);
		m.referenceFrame=xml_attr_i(m.profile,"referenceFrame",-1); m.firstFrame=xml_attr_i(m.profile,"firstFrame",0);
		m.contactFade=xml_attr_f(m.profile,"contactFade",CAPTURE_CONTACT_FADE);
		m.contactHeight=xml_attr_f(m.profile,"contactHeight",CAPTURE_CONTACT_HEIGHT);
		m.contactSpeed=xml_attr_f(m.profile,"contactSpeed",CAPTURE_CONTACT_SPEED);
		if(!m.refPos || !m.pos || !m.refRot || !m.rot || m.referenceFrame < -1 || m.referenceFrame>=m.clip->nframes || m.firstFrame<0 || m.firstFrame>=m.clip->nframes ||
			!isfinite(m.contactFade) || m.contactFade<0 || !isfinite(m.contactHeight) || m.contactHeight<=0 || !isfinite(m.contactSpeed) || m.contactSpeed<=0){
			fprintf(stderr,"[mocap] invalid profile or allocation for %s\n",path); fflush(stderr); m.valid=0;
		}
		if(m.valid){
			mocap_map(&m); bvh_reference(m.clip,m.referenceFrame,m.refPos,m.refRot);
			int required[]={MOCAP_HIPS,MOCAP_UPLEG,MOCAP_UPLEG+1,MOCAP_LEG,MOCAP_LEG+1,MOCAP_FOOT,MOCAP_FOOT+1};
			for(size_t i=0;i<sizeof(required)/sizeof(required[0]);i++) if(m.role[required[i]]<0){
				fprintf(stderr,"[mocap] %s: missing role %s; add a capture-profile map\n",path,capture_roles[required[i]]); fflush(stderr); m.valid=0;
			}
		}
		if(m.valid){
			int *r=m.role;
			vec3 up=capture_axis(xml_attr(m.profile,"up","y")),left=vsub(m.refPos[r[MOCAP_UPLEG]],m.refPos[r[MOCAP_UPLEG+1]]);
			if(xml_attr(m.profile,"forward",NULL)) left=vcross(up,xml_attr_v3(m.profile,"forward",v3(0,0,0)));
			left=vnorm(vsub(left,vscale(up,vdot(left,up)))); vec3 back=vcross(up,left);
			m.toBody=mat4_identity();
			m.toBody.m[0]=left.x; m.toBody.m[4]=left.y; m.toBody.m[8]=left.z;
			m.toBody.m[1]=back.x; m.toBody.m[5]=back.y; m.toBody.m[9]=back.z;
			m.toBody.m[2]=up.x;   m.toBody.m[6]=up.y;   m.toBody.m[10]=up.z;
			m.legLength=vlen(vsub(m.refPos[r[MOCAP_UPLEG]],m.refPos[r[MOCAP_LEG]]))+vlen(vsub(m.refPos[r[MOCAP_LEG]],m.refPos[r[MOCAP_FOOT]]));
			if(vlen(back)<RIG_EPSILON || m.legLength<RIG_EPSILON){ fprintf(stderr,"[mocap] degenerate reference axes or leg lengths: %s\n",path); fflush(stderr); m.valid=0; }
			if(m.valid) capture_contacts(&m);
		}
		int fingers=0; for(int side=0;side<2;side++) for(int d=0;d<MOCAP_DIGITS;d++) for(int k=0;k<MOCAP_KNUCKLES;k++) fingers+=m.digits[side][d][k]>=0;
		fprintf(stderr,"[mocap] %s profile=%s valid=%d joints=%d frames=%d finger_channels=%d/%d contacts=%d reference=%d first=%d\n",path,profileName,m.valid,n,m.clip->nframes,fingers,2*MOCAP_DIGITS*MOCAP_KNUCKLES,m.ncontacts,m.referenceFrame,m.firstFrame); fflush(stderr);
	}
	DA_PUSH(s->mocap,s->nmocap,s->cmocap,m);
	return m.valid?&s->mocap[s->nmocap-1]:NULL;
}

/* A source direction at arc-length fraction [a,b] of a joint polyline, in the body frame. */
static vec3 mocap_polyline_dir(const MocapSource *m,int start,const int *chain,int n,float a,float b){
	vec3 points[MOCAP_MAX_CHAIN+1]; float lengths[MOCAP_MAX_CHAIN+1],total=0;
	points[0]=m->pos[start]; lengths[0]=0;
	for(int i=0;i<n;i++){ points[i+1]=m->pos[chain[i]]; total+=vlen(vsub(points[i+1],points[i])); lengths[i+1]=total; }
	vec3 at[2]; float want[2]={a*total,b*total};
	for(int k=0;k<2;k++){
		at[k]=points[n];
		for(int i=0;i<n;i++) if(want[k]<=lengths[i+1]){
			float span=lengths[i+1]-lengths[i];
			at[k]=lerp(points[i],points[i+1],span>0?(want[k]-lengths[i])/span:0); break;
		}
	}
	return vnorm(mat4_xform_dir(m->toBody,vsub(at[1],at[0])));
}

static vec3 mocap_bone_dir(const MocapSource *m,int from,int to){
	return vnorm(mat4_xform_dir(m->toBody,vsub(m->pos[to],m->pos[from])));
}

/* Source rotation since the reference frame, expressed in the body frame. */
static mat4 mocap_hub_rotation(const MocapSource *m,int joint){
	mat4 delta=mat4_mul(mat4_from_quat(m->rot[joint]),mat4_affine_inverse(mat4_from_quat(m->refRot[joint])));
	return mat4_mul(m->toBody,mat4_mul(delta,mat4_affine_inverse(m->toBody)));
}

/* An upper hub points its up axis along the source bone above it and faces where the source
   joint faces; reference poses often tilt the chest or head, so their full rotation misleads. */
static mat4 mocap_hub_frame(const MocapSource *m,int joint,int above){
	mat4 turn=mocap_hub_rotation(m,joint);
	if(above<0) return turn;
	vec3 up=mocap_bone_dir(m,joint,above),forward=mat4_xform_dir(turn,v3(0,-1,0));
	vec3 back=vscale(vsub(forward,vscale(up,vdot(forward,up))),-1);
	if(vlen(back)<RIG_EPSILON) return turn;
	back=vnorm(back);
	vec3 left=vcross(back,up);
	mat4 frame=mat4_identity();
	frame.m[0]=left.x; frame.m[1]=left.y; frame.m[2]=left.z;
	frame.m[4]=back.x; frame.m[5]=back.y; frame.m[6]=back.z;
	frame.m[8]=up.x;   frame.m[9]=up.y;   frame.m[10]=up.z;
	return frame;
}

typedef struct { const MocapSource *m; XmlNode *root; AnimPose *out; int fingers; } MocapWalk;

static void mocap_set(MocapWalk *w,XmlNode *bone,mat4 parentWorld,mat4 world,mat4 *result){
	mat4 delta=mat4_mul(mat4_affine_inverse(parentWorld),world);
	delta.m[12]=delta.m[13]=delta.m[14]=0;
	AnimJoint *j=anim_joint(w->out,xml_attr(bone,"name",""));
	j->q=quat_from_mat4(delta);
	*result=world;
}

static void mocap_aim(MocapWalk *w,XmlNode *bone,mat4 parentWorld,vec3 direction,mat4 *result){
	BoneGeom g=bone_geom(bone);
	vec3 local=mat4_xform_dir(mat4_affine_inverse(parentWorld),direction);
	mat4 delta=rig_from_to(g.dir,local,g.other);
	AnimJoint *j=anim_joint(w->out,xml_attr(bone,"name",""));
	j->q=quat_from_mat4(delta);
	*result=mat4_mul(parentWorld,delta);
}

static XmlNode *capture_target_map(const MocapSource *m,const char *target){
	XmlNode *p=m->profile;
	for(int i=0;p && i<p->nkids;i++) if(!strcmp(p->kids[i]->tag,"map") && !strcmp(xml_attr(p->kids[i],"target",""),target)) return p->kids[i];
	return NULL;
}

static mat4 capture_bone_world(const MocapSource *m,XmlNode *bone,int from,int to){
	vec3 reference=v3(0,0,0);
	for(int guard=0;to>=0 && guard<MOCAP_MAX_CHAIN;guard++){
		reference=vsub(m->refPos[to],m->refPos[from]);
		if(vlen(reference)>RIG_EPSILON) break;
		to=mocap_first_child(m->clip,to);
	}
	if(vlen(reference)<RIG_EPSILON){
		int parent=m->clip->joints[from].parent;
		reference=parent>=0?vsub(m->refPos[from],m->refPos[parent]):v3(1,0,0);
	}
	BoneGeom g=bone_geom(bone);
	mat4 align=rig_from_to(g.dir,mat4_xform_dir(m->toBody,reference),g.other);
	XmlNode *map=capture_target_map(m,xml_attr(bone,"name",""));
	if(map) align=mat4_mul(align,mat4_rot_xyz(xml_attr_v3(map,"rotation",v3(0,0,0))));
	return mat4_mul(mocap_hub_rotation(m,from),align);
}

static int capture_digit_joint(const MocapSource *m,const char *name,int side){
	const char *part=strchr(name,'_');
	if(!part) return -1;
	part++;
	for(int d=0;d<MOCAP_DIGITS;d++){
		size_t n=strlen(capture_digits[d]);
		if(strncmp(part,capture_digits[d],n) || (part[n] && part[n]!='_')) continue;
		int link=part[n]=='_'?atoi(part+n+1)-1:0;
		return link>=0 && link<MOCAP_KNUCKLES?m->digits[side][d][link]:-1;
	}
	return -1;
}

/* Walk the rig from a hub: hubDepth 0 is the pelvis, 1 the ribcage, 2 the head. */
static void mocap_walk(MocapWalk *w,XmlNode *node,mat4 parentWorld,int hubDepth,int link,int links,int limbSide,int limbArm,int limbIndex){
	if(!rig_node_enabled(node,w->fingers)) return;
	const MocapSource *m=w->m; const int *r=m->role;
	mat4 world=parentWorld;
	int named=xml_attr(node,"name",NULL)!=NULL;
	XmlNode *map=named?capture_target_map(m,xml_attr(node,"name","")):NULL;
	if(map){
		int from=capture_named(m->clip,xml_attr(map,"source",""));
		int to=xml_attr(map,"tip",NULL)?capture_named(m->clip,xml_attr(map,"tip","")):mocap_first_child(m->clip,from);
		if(from>=0) mocap_set(w,node,parentWorld,capture_bone_world(m,node,from,to),&world);
		if(!strcmp(node->tag,"hub")){ hubDepth++; link=0; }
	} else if(named && !strcmp(node->tag,"digit") && limbSide>=0){
		int from=capture_digit_joint(m,xml_attr(node,"name",""),limbSide);
		if(from>=0) mocap_set(w,node,parentWorld,capture_bone_world(m,node,from,mocap_first_child(m->clip,from)),&world);
	} else if(named && !strcmp(node->tag,"hub")){
		int joint=hubDepth==0?r[MOCAP_HIPS]:hubDepth==1?r[MOCAP_CHEST]:hubDepth==2?r[MOCAP_HEAD]:-1;
		int above=hubDepth==1?(m->nneck?m->neck[0]:-1):hubDepth==2?r[MOCAP_HEAD_END]:-1;
		if(joint>=0) mocap_set(w,node,parentWorld,mocap_hub_frame(m,joint,above),&world);
		hubDepth++; link=0;
	} else if(named && !strcmp(node->tag,"spine") && links>0){
		const int *chain=hubDepth==1?m->spine:m->neck; int n=hubDepth==1?m->nspine:m->nneck;
		int start=hubDepth==1?r[MOCAP_HIPS]:r[MOCAP_CHEST];
		if(n>0 && start>=0) mocap_aim(w,node,parentWorld,mocap_polyline_dir(m,start,chain,n,(float)link/links,(float)(link+1)/links),&world);
		link++;
	} else if(named && limbSide>=0 && strcmp(node->tag,"digit") && !bone_at(node)){
		int from=-1,to=-1,side=limbSide;
		if(limbArm){
			if(!strcmp(node->tag,"collarbone")){ from=r[MOCAP_COLLAR+side]; to=r[MOCAP_ARM+side]; limbIndex=-1; }
			else if(!strcmp(node->tag,"palm")){ from=r[MOCAP_HAND+side]; to=r[MOCAP_HAND_END+side]; }
			else if(limbIndex==0){ from=r[MOCAP_ARM+side]; to=r[MOCAP_FOREARM+side]; }
			else if(limbIndex==1){ from=r[MOCAP_FOREARM+side]; to=r[MOCAP_HAND+side]; }
		} else {
			if(!strcmp(node->tag,"ankle")){ from=r[MOCAP_FOOT+side]; to=r[MOCAP_TOE+side]; }
			else if(limbIndex==0){ from=r[MOCAP_UPLEG+side]; to=r[MOCAP_LEG+side]; }
			else if(limbIndex==1){ from=r[MOCAP_LEG+side]; to=r[MOCAP_FOOT+side]; }
		}
		if(from>=0) mocap_set(w,node,parentWorld,capture_bone_world(m,node,from,to),&world);
		limbIndex++;
	}
	for(int i=0;i<node->nkids;i++){
		XmlNode *kid=node->kids[i];
		if(!xml_is_bone(kid)) continue;
		if(xml_is_limb(kid)){
			const char *name=xml_attr(kid,"name","");
			int side=!strncmp(name,"right_",6)?1:!strncmp(name,"left_",5)?0:-1;
			mocap_walk(w,kid,world,hubDepth,0,0,side,strcmp(rig_limb_type(kid),"leg")!=0,0);
			continue;
		}
		int kidLinks=links;
		if(!strcmp(kid->tag,"spine") && strcmp(node->tag,"spine")){
			kidLinks=0;
			for(XmlNode *walk=kid;walk;){
				kidLinks++;
				XmlNode *next=NULL;
				for(int k=0;k<walk->nkids && !next;k++) if(!strcmp(walk->kids[k]->tag,"spine")) next=walk->kids[k];
				walk=next;
			}
		}
		mocap_walk(w,kid,world,hubDepth,!strcmp(kid->tag,"spine")?link:0,kidLinks,limbSide,limbArm,limbIndex);
	}
}

/* How far a joint moved from the reference frame, body frame, scaled; heights from the clip floor. */
static vec3 mocap_moved(const MocapSource *m,int joint,float scale){
	vec3 moved=mat4_xform_dir(m->toBody,vsub(m->pos[joint],m->refPos[joint]));
	moved.z+=m->groundShift;
	return vscale(moved,scale);
}

static void capture_pole(MocapSource *m,XmlNode *ik,int effector,mat4 cycle){
	int hip=capture_hip(m,effector),knee=m->role[(effector<2?MOCAP_LEG:MOCAP_FOREARM)+effector%2],end=capture_effector(m,effector);
	if(hip<0 || knee<0 || end<0) return;
	vec3 axis=vnorm(vsub(m->pos[end],m->pos[hip])),upper=vsub(m->pos[knee],m->pos[hip]);
	vec3 pole=vsub(upper,vscale(axis,vdot(upper,axis)));
	if(vlen(pole)<m->legLength*CAPTURE_POLE_EPSILON){
		vec3 body=mat4_xform_dir(mocap_hub_rotation(m,hip),v3(0,effector<2?-1:1,0));
		pole=mat4_xform_dir(mat4_affine_inverse(m->toBody),body);
	}
	pole=vnorm(mat4_xform_dir(cycle,mat4_xform_dir(m->toBody,pole)));
	char value[64];
	snprintf(value,sizeof(value),"%.7g %.7g",atan2f(pole.x,-pole.y)*180/M_PIf,asinf(fmaxf(-1,fminf(1,pole.z)))*180/M_PIf);
	xml_set_attr(ik,"bend",value);
}

static mat4 capture_pose_world(Scene *s,XmlNode *node,XmlNode *root,const AnimPose *pose){
	if(!node || node==root) return mat4_identity();
	mat4 local=xml_node_transform(s,node);
	AnimJoint *j=anim_joint_find(pose,xml_attr(node,"name",""));
	if(j) local=mat4_mul(local,mat4_mul(mat4_translate(j->pos),mat4_from_quat(j->q)));
	return mat4_mul(capture_pose_world(s,node->parent,root,pose),local);
}

static vec3 capture_support(Scene *s,XmlNode *node,XmlNode *root,const AnimPose *pose){
	mat4 world=capture_pose_world(s,node,root,pose);
	vec3 lowest=mat4_xform_point(world,v3(0,0,0)); lowest.z=INFINITY;
	if(bone_has_volume(node)){
		BoneGeom g=bone_geom(node);
		vec3 center=mat4_xform_point(world,g.center);
		vec3 a=mat4_xform_dir(world,vscale(g.side,g.radiusSide)),b=mat4_xform_dir(world,vscale(g.dir,g.along)),c=mat4_xform_dir(world,vscale(g.other,g.radiusOther));
		float radius=sqrtf(a.z*a.z+b.z*b.z+c.z*c.z);
		lowest=radius>RIG_EPSILON?vsub(center,vscale(vadd(vadd(vscale(a,a.z),vscale(b,b.z)),vscale(c,c.z)),1/radius)):center;
	}
	for(int i=0;i<node->nkids;i++) if(xml_is_bone(node->kids[i]) && rig_node_enabled(node->kids[i],rig_fingers_enabled(root,s->activeRigInstance))){
		vec3 p=capture_support(s,node->kids[i],root,pose); if(p.z<lowest.z) lowest=p;
	}
	return lowest;
}

static void capture_sample(Scene *s,XmlNode *proot,MocapSource *m,float time,float scale,AnimPose *out){
	bvh_evaluate(m->clip,time,m->pos,m->rot);
	XmlNode *root=rig_root_bone(proot);
	MocapWalk walk={m,root,out,rig_fingers_enabled(proot,s->activeRigInstance)}; mocap_walk(&walk,root,mat4_identity(),0,0,0,-1,0,0);
	anim_joint(out,xml_attr(root,"name",""))->pos=mocap_moved(m,m->role[MOCAP_HIPS],scale);
	(void)s;
}

static float capture_contact_weight(float t,float begin,float end,float fade){
	if(t<begin || t>end) return 0;
	if(fade<=0) return 1;
	return motion_ease((t-begin)/fade)*motion_ease((end-t)/fade);
}

static float capture_contact_at(MocapSource *m,XmlNode *layer,const char *limb,int effector,float t,float from,float to,float *anchor,const char **mode,vec3 *offset){
	int manual=0; float weight=0;
	*mode="pivot"; *offset=v3(0,0,0);
	for(int i=0;i<layer->nkids;i++){
		XmlNode *n=layer->kids[i];
		if(strcmp(n->tag,"contact") || strcmp(xml_attr(n,"limb",""),limb)) continue;
		manual=1;
		float begin=fmaxf(from,xml_attr_f(n,"start",from)),end=fminf(to,xml_attr_f(n,"end",to));
		float fade=xml_attr_f(n,"fade",m->contactFade),strength=xml_attr_f(n,"weight",1);
		if(!isfinite(begin) || !isfinite(end) || !isfinite(fade) || fade<0 || !isfinite(strength) || strength<0 || strength>1){
			fprintf(stderr,"[mocap] invalid contact interval or weight for %s\n",limb); fflush(stderr); continue;
		}
		float w=capture_contact_weight(t,begin,end,fade)*strength;
		if(w<=weight) continue;
		weight=w; *anchor=begin; *mode=xml_attr(n,"mode","plant"); *offset=xml_attr_v3_cm(n,"offset",v3(0,0,0));
	}
	if(manual) return weight;
	if(!strcmp(xml_attr(layer,"contacts","auto"),"none")) return 0;
	for(int i=0;i<m->ncontacts;i++){
		MocapContact *c=&m->contacts[i]; if(c->effector!=effector) continue;
		float begin=fmaxf(from,c->start*m->clip->frameTime),end=fminf(to,c->end*m->clip->frameTime);
		float w=capture_contact_weight(t,begin,end,m->contactFade);
		if(w>weight){ weight=w; *anchor=begin; }
	}
	return weight;
}

static mat4 capture_cycle(Scene *s,XmlNode *proot,MocapSource *m,float from,float to,float scale,int cycles){
	if(!cycles) return mat4_identity();
	AnimPose first={0},last={0};
	capture_sample(s,proot,m,from,scale,&first); capture_sample(s,proot,m,to,scale,&last);
	XmlNode *root=rig_root_bone(proot);
	mat4 a=capture_pose_world(s,root,proot,&first),b=capture_pose_world(s,root,proot,&last);
	vec3 af=mat4_xform_dir(a,v3(0,-1,0)),bf=mat4_xform_dir(b,v3(0,-1,0));
	float angle=(atan2f(bf.x,-bf.y)-atan2f(af.x,-af.y))*180/M_PIf;
	mat4 step=mat4_mul(mat4_translate(v3(b.m[12],b.m[13],0)),mat4_mul(mat4_rot_z(angle),mat4_translate(v3(-a.m[12],-a.m[13],0))));
	if(cycles<0){ step=mat4_affine_inverse(step); cycles=-cycles; }
	mat4 result=mat4_identity();
	while(cycles){ if(cycles&1) result=mat4_mul(result,step); step=mat4_mul(step,step); cycles>>=1; }
	anim_pose_free(&first); anim_pose_free(&last); return result;
}

typedef struct {
	XmlNode *limb,*end;
	int effector;
	vec3 hip,raw,goal;
	float upper,lower,weight;
} CaptureGoal;

static void capture_hand_pose(Scene *s,XmlNode *proot,XmlNode *layer,AnimPose *out){
	const char *handPose=xml_attr(layer,"handPose",NULL);
	if(handPose){
		XmlNode *pose=rig_find_pose(s,proot,handPose); AnimPose hand={0};
		if(pose){ anim_pose_read(s,proot,pose,0,&hand); anim_pose_apply(proot,out,&hand,1,0,"left_palm right_palm"); }
		else { fprintf(stderr,"[mocap] unknown hand pose '%s'\n",handPose); fflush(stderr); }
		anim_pose_free(&hand);
	}
}

static void mocap_retarget(Scene *s,XmlNode *proot,XmlNode *layer,float seconds,AnimPose *out){
	MocapSource *m=mocap_source(s,proot,layer);
	XmlNode *root=rig_root_bone(proot);
	if(!m || !root) return;
	bvh_clip_t *c=m->clip;
	float from=xml_attr_f(layer,"from",m->firstFrame*c->frameTime),to=xml_attr_f(layer,"to",bvh_duration(c)),speed=xml_attr_f(layer,"speed",1);
	if(!isfinite(from) || !isfinite(to) || !isfinite(speed) || from<0 || to<from || to>bvh_duration(c)){
		fprintf(stderr,"[mocap] invalid playback range from=%g to=%g speed=%g\n",from,to,speed); fflush(stderr); return;
	}
	float elapsed=seconds*speed,t=from+elapsed; int cycles=0;
	if(xml_attr_i(layer,"loop",0) && to>from){
		float count=floorf(elapsed/(to-from));
		if(!isfinite(count) || fabsf(count)>CAPTURE_MAX_CYCLES){ fprintf(stderr,"[mocap] excessive loop count %g\n",count); fflush(stderr); return; }
		cycles=(int)count; t=from+elapsed-count*(to-from);
	}
	t=fmaxf(from,fminf(t,to));
	GaitLeg limbs[GAIT_MAX_LIMBS]; int nlimbs=0;
	rig_collect_limbs(proot,"leg",limbs,&nlimbs);
	float reach=0;
	for(int i=0;i<nlimbs;i++){
		XmlNode *end=rig_limb_end(limbs[i].limb);
		if(!end || !end->parent || !end->parent->parent) continue;
		vec3 h=mat4_xform_point(rig_rest_world(s,end->parent->parent,proot),v3(0,0,0)),k=mat4_xform_point(rig_rest_world(s,end->parent,proot),v3(0,0,0)),a=mat4_xform_point(rig_rest_world(s,end,proot),v3(0,0,0));
		reach=fmaxf(reach,vlen(vsub(k,h))+vlen(vsub(a,k)));
	}
	float scale=xml_attr_f_cm(m->profile,"scale",reach/m->legLength);
	if(!isfinite(scale) || scale<=0){ fprintf(stderr,"[mocap] invalid target/source scale %g\n",scale); fflush(stderr); return; }
	const char *rootMode=xml_attr(layer,"rootMotion",xml_attr_i(layer,"inPlace",0)?"inPlace":"accumulate");
	if(strcmp(rootMode,"accumulate") && strcmp(rootMode,"repeat") && strcmp(rootMode,"inPlace")){
		fprintf(stderr,"[mocap] rootMotion='%s'; use accumulate, repeat or inPlace\n",rootMode); fflush(stderr); return;
	}
	mat4 cycle=capture_cycle(s,proot,m,from,to,scale,!strcmp(rootMode,"accumulate")?cycles:0);
	capture_sample(s,proot,m,t,scale,out);
	float loopBlend=xml_attr_f(layer,"loopBlend",CAPTURE_LOOP_BLEND);
	if(!isfinite(loopBlend) || loopBlend<0){ fprintf(stderr,"[mocap] invalid loopBlend %g\n",loopBlend); fflush(stderr); return; }
	if(xml_attr_i(layer,"loop",0) && loopBlend>0 && to>from && t>to-fminf(loopBlend,(to-from)*0.5f)){
		float span=fminf(loopBlend,(to-from)*0.5f),weight=motion_ease((t-(to-span))/span);
		mat4 nextCycle=capture_cycle(s,proot,m,from,to,scale,1);
		AnimPose next={0},mixed={0}; capture_sample(s,proot,m,from,scale,&next);
		AnimJoint *nextRoot=anim_joint(&next,xml_attr(root,"name",""));
		vec3 rest=mat4_xform_point(rig_rest_world(s,root,proot),v3(0,0,0));
		nextRoot->pos=vsub(mat4_xform_point(nextCycle,vadd(rest,nextRoot->pos)),rest);
		nextRoot->q=quat_mul(quat_from_mat4(nextCycle),nextRoot->q);
		anim_pose_mix(proot,out,&next,weight,&mixed); anim_pose_free(out); *out=mixed; anim_pose_free(&next);
		bvh_evaluate(c,t,m->pos,m->rot);
	}
	capture_hand_pose(s,proot,layer,out);
	const char *legMode=xml_attr(layer,"legs","ik"),*hands=xml_attr(layer,"hands","contact"),*contacts=xml_attr(layer,"contacts","auto");
	if((strcmp(legMode,"ik") && strcmp(legMode,"fk")) || (strcmp(hands,"contact") && strcmp(hands,"fk")) || (strcmp(contacts,"auto") && strcmp(contacts,"none"))){
		fprintf(stderr,"[mocap] invalid legs=%s hands=%s contacts=%s\n",legMode,hands,contacts); fflush(stderr); return;
	}
	int nlegs=nlimbs; rig_collect_limbs(proot,"arm",limbs,&nlimbs);
	CaptureGoal goals[GAIT_MAX_LIMBS]; int ngoals=0;
	float ground=xml_attr_f_cm(layer,"ground",0),contactWeight=fmaxf(0,fminf(1,xml_attr_f(layer,"contactWeight",1)));
	for(int i=0;i<nlimbs;i++){
		if((i<nlegs && !strcmp(legMode,"fk")) || (i>=nlegs && !strcmp(hands,"fk"))) continue;
		const char *name=xml_attr(limbs[i].limb,"name","");
		if(strncmp(name,"left_",5) && strncmp(name,"right_",6)) continue;
		int e=(i<nlegs?0:2)+!strncmp(name,"right_",6),foot=capture_effector(m,e),hip=capture_hip(m,e);
		XmlNode *end=rig_limb_end(limbs[i].limb);
		if(foot<0 || hip<0 || !end || !end->parent || !end->parent->parent) continue;
		mat4 hm=capture_pose_world(s,end->parent->parent,proot,out),km=capture_pose_world(s,end->parent,proot,out),em=capture_pose_world(s,end,proot,out);
		vec3 h=mat4_xform_point(hm,v3(0,0,0)),k=mat4_xform_point(km,v3(0,0,0)),a=mat4_xform_point(em,v3(0,0,0));
		vec3 raw=i<nlegs?vadd(h,vscale(mat4_xform_dir(m->toBody,vsub(m->pos[foot],m->pos[hip])),scale)):a;
		float seam=xml_attr_i(layer,"loop",0) && loopBlend>0 && to>from?motion_ease((t-(to-fminf(loopBlend,(to-from)*0.5f)))/fminf(loopBlend,(to-from)*0.5f)):0;
		raw=lerp(raw,a,seam);
		const char *mode; vec3 offset; float anchor=t;
		float weight=capture_contact_at(m,layer,name,e,t,from,to,&anchor,&mode,&offset)*contactWeight;
		if(i>=nlegs && weight<=0) continue;
		vec3 goal=raw;
		if(weight>0){
			AnimPose plant={0}; capture_sample(s,proot,m,anchor,scale,&plant); capture_hand_pose(s,proot,layer,&plant);
			mat4 planted=capture_pose_world(s,end,proot,&plant);
			vec3 support=capture_support(s,end,proot,&plant),pivot=mat4_xform_point(mat4_affine_inverse(planted),support);
			support.z=ground; support=vadd(support,offset);
			if(!strcmp(mode,"plant")){
				mat4 parent=capture_pose_world(s,end->parent,proot,out);
				AnimJoint *j=anim_joint(out,xml_attr(end,"name",""));
				j->q=quat_slerp(j->q,quat_from_mat4(mat4_mul(mat4_affine_inverse(parent),planted)),weight);
				em=capture_pose_world(s,end,proot,out);
			} else if(strcmp(mode,"pivot") && strcmp(mode,"slide")){
				fprintf(stderr,"[mocap] contact mode='%s'; use plant, pivot or slide\n",mode); fflush(stderr); weight=0;
			}
			goal=vsub(support,mat4_xform_dir(em,pivot));
			if(!strcmp(mode,"slide")){ goal.x=raw.x+offset.x; goal.y=raw.y+offset.y; }
			vec3 currentSupport=capture_support(s,end,proot,out);
			goal.z=fmaxf(goal.z,a.z+ground-currentSupport.z+offset.z);
			goal=lerp(raw,goal,weight);
			anim_pose_free(&plant); bvh_evaluate(c,t,m->pos,m->rot);
		}
		CaptureGoal g={limbs[i].limb,end,e,h,raw,goal,vlen(vsub(k,h)),vlen(vsub(a,k)),weight}; goals[ngoals++]=g;
	}
	vec3 correction=v3(0,0,0);
	for(int iteration=0;iteration<CAPTURE_REACH_ITERATIONS;iteration++) for(int i=0;i<ngoals;i++){
		CaptureGoal *g=&goals[i]; if(g->weight<=0) continue;
		vec3 v=vsub(g->goal,vadd(g->hip,vscale(correction,g->weight))); float length=vlen(v);
		float maxReach=g->upper+g->lower-CAPTURE_REACH_MARGIN,minReach=fabsf(g->upper-g->lower)+CAPTURE_REACH_MARGIN;
		float error=length-fmaxf(minReach,fminf(maxReach,length));
		if(length>RIG_EPSILON) correction=vadd(correction,vscale(v,error/length*g->weight));
	}
	AnimJoint *pelvis=anim_joint(out,xml_attr(root,"name","")); pelvis->pos=vadd(pelvis->pos,correction);
	vec3 rest=mat4_xform_point(rig_rest_world(s,root,proot),v3(0,0,0));
	vec3 position=mat4_xform_point(cycle,vadd(rest,pelvis->pos));
	vec3 drift=!strcmp(rootMode,"inPlace")?v3(position.x-rest.x,position.y-rest.y,0):v3(0,0,0);
	pelvis->pos=vsub(vsub(position,rest),drift); pelvis->q=quat_mul(quat_from_mat4(cycle),pelvis->q);
	for(int i=0;i<ngoals;i++){
		CaptureGoal *g=&goals[i];
		vec3 goal=vadd(g->goal,vscale(correction,1-g->weight));
		goal=vsub(mat4_xform_point(cycle,goal),drift);
		vec3 endRest=mat4_xform_point(rig_rest_world(s,g->end,proot),v3(0,0,0));
		XmlNode *ik=xml_new("ik"); xml_set_attr(ik,"limb",xml_attr(g->limb,"name",""));
		xml_set_attr_v3(ik,"offset",vscale(vsub(goal,endRest),CM_PER_METRE));
		capture_pole(m,ik,g->effector,cycle);
		anim_put_ik(out,rig_ik_tip_name(proot,ik),ik);
	}
}

#endif
