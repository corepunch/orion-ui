#ifndef __SCENE_MOTION_H__
#define __SCENE_MOTION_H__

/* ---------------------------------------------- Timeline: layers and gait -- */
/* An instance's <layer> and <gait> children play over scene time. They evaluate, in
   document order on top of the instance's pose, into one generated pose per frame. */

typedef struct { const char *name; quat q; vec3 pos; } AnimJoint;
typedef struct { char tip[BONE_NAME_CAPACITY]; XmlNode *ik; } AnimIk;
typedef struct { AnimJoint *joints; int njoints,cjoints; AnimIk *iks; int niks,ciks; } AnimPose;
typedef struct { XmlNode *limb; float phase; } GaitLeg;

static void anim_pose_free(AnimPose *p){
	for(int i=0;i<p->niks;i++) xml_free(p->iks[i].ik);
	free(p->joints); free(p->iks); memset(p,0,sizeof(*p));
}

static AnimJoint *anim_joint_find(const AnimPose *p,const char *name){
	for(int i=0;i<p->njoints;i++) if(!strcmp(p->joints[i].name,name)) return &p->joints[i];
	return NULL;
}

static AnimJoint *anim_joint(AnimPose *p,const char *name){
	AnimJoint *found=anim_joint_find(p,name);
	if(found) return found;
	AnimJoint j={name,quat_identity(),v3(0,0,0)};
	DA_PUSH(p->joints,p->njoints,p->cjoints,j);
	return &p->joints[p->njoints-1];
}

static AnimIk *anim_ik_find(const AnimPose *p,const char *tip){
	for(int i=0;i<p->niks;i++) if(!strcmp(p->iks[i].tip,tip)) return &p->iks[i];
	return NULL;
}

/* Takes ownership of ik. */
static void anim_put_ik(AnimPose *p,const char *tip,XmlNode *ik){
	AnimIk *found=anim_ik_find(p,tip);
	if(found){ xml_free(found->ik); found->ik=ik; return; }
	AnimIk entry; snprintf(entry.tip,sizeof(entry.tip),"%s",tip); entry.ik=ik;
	DA_PUSH(p->iks,p->niks,p->ciks,entry);
}

static const char *rig_mirror_joint_name(const char *name,char *buffer,size_t size){
	if(strncmp(name,"left_",5) && strncmp(name,"right_",6)) return name;
	rig_mirror_name(name,buffer,size);
	return buffer;
}

static void anim_pose_read(Scene *s,XmlNode *proot,XmlNode *source,int mirror,AnimPose *out){
	for(int i=0;source && i<source->nkids;i++){
		XmlNode *kid=source->kids[i];
		if(!strcmp(kid->tag,"joint")){
			char pairName[BONE_NAME_CAPACITY];
			const char *target=xml_attr(kid,"target","");
			XmlNode *bone=rig_find_joint(proot,target);
			if(!bone){ fprintf(stderr,"[scener] pose joint '%s' is not in the rig\n",target); fflush(stderr); continue; }
			if(!rig_node_enabled(bone,rig_fingers_enabled(proot,s->activeRigInstance))) continue;
			mat4 turn,delta=rig_override_delta(s,bone,kid,&turn);
			quat q=quat_from_mat4(turn); vec3 pos=v3(delta.m[12],delta.m[13],delta.m[14]);
			if(mirror){
				XmlNode *pair=rig_find_joint(proot,rig_mirror_joint_name(xml_attr(bone,"name",""),pairName,sizeof(pairName)));
				if(pair) bone=pair;
				q=quat_mirror_x(q); pos.x=-pos.x;
			}
			AnimJoint *j=anim_joint(out,xml_attr(bone,"name",""));
			j->q=q; j->pos=pos;
		} else if(!strcmp(kid->tag,"ik")){
			XmlNode *copy=xml_clone(kid,NULL);
			xml_remove_attr(copy,"generated");
			if(mirror) rig_mirror_pose_item(copy);
			anim_put_ik(out,rig_ik_tip_name(proot,copy),copy);
		}
	}
}

/* Reflect a built pose across the body's left-right plane, as mirror="1" does for poses. */
static void anim_pose_mirror(XmlNode *proot,AnimPose *p){
	char pairName[BONE_NAME_CAPACITY];
	for(int i=0;i<p->njoints;i++){
		XmlNode *pair=rig_find_joint(proot,rig_mirror_joint_name(p->joints[i].name,pairName,sizeof(pairName)));
		if(pair) p->joints[i].name=xml_attr(pair,"name","");
		p->joints[i].q=quat_mirror_x(p->joints[i].q); p->joints[i].pos.x=-p->joints[i].pos.x;
	}
	for(int i=0;i<p->niks;i++){
		rig_mirror_pose_item(p->iks[i].ik);
		snprintf(p->iks[i].tip,sizeof(p->iks[i].tip),"%s",rig_ik_tip_name(proot,p->iks[i].ik));
	}
}

static int anim_word_in(const char *list,const char *word){
	size_t length=strlen(word);
	for(const char *p=list;*p;){
		while(*p && isspace((unsigned char)*p)) p++;
		const char *start=p;
		while(*p && !isspace((unsigned char)*p)) p++;
		if((size_t)(p-start)==length && !strncmp(start,word,length)) return 1;
	}
	return 0;
}

/* mask="right_arm head" limits a layer to those joints and everything below them. */
static int anim_masked(XmlNode *proot,const char *mask,const char *joint){
	if(!mask || !*mask) return 1;
	for(XmlNode *n=rig_find_joint(proot,joint);n && n!=proot;n=n->parent){
		const char *name=xml_attr(n,"name",NULL);
		if(name && anim_word_in(mask,name)) return 1;
	}
	return 0;
}

static void anim_mix_v3(XmlNode *into,XmlNode *from,const char *attribute,float u){
	if(!xml_attr(into,attribute,NULL) || !xml_attr(from,attribute,NULL)) return;
	xml_set_attr_v3(into,attribute,lerp(xml_attr_v3(from,attribute,v3(0,0,0)),xml_attr_v3(into,attribute,v3(0,0,0)),u));
}

/* into becomes from blended toward into by u; attributes into lacks come from from. */
static void anim_ik_mix(XmlNode *into,XmlNode *from,float u){
	static const char *vectors[]={"offset","target","pole"};
	for(size_t i=0;i<sizeof(vectors)/sizeof(vectors[0]);i++){
		if(!xml_attr(into,vectors[i],NULL) && xml_attr(from,vectors[i],NULL)) xml_set_attr(into,vectors[i],xml_attr(from,vectors[i],""));
		else anim_mix_v3(into,from,vectors[i],u);
	}
	float a0,e0,a1,e1; char value[64];
	if(xml_attr_2f(from,"bend",0,0,&a0,&e0) && xml_attr_2f(into,"bend",a0,e0,&a1,&e1)){
		snprintf(value,sizeof(value),"%.6g %.6g",a0+(a1-a0)*u,e0+(e1-e0)*u); xml_set_attr(into,"bend",value);
	}
	snprintf(value,sizeof(value),"%.6g",xml_attr_f(from,"weight",1)+(xml_attr_f(into,"weight",1)-xml_attr_f(from,"weight",1))*u);
	xml_set_attr(into,"weight",value);
}

static void anim_scale_weight(XmlNode *ik,float scale){
	char value[32]; snprintf(value,sizeof(value),"%.6g",xml_attr_f(ik,"weight",1)*scale); xml_set_attr(ik,"weight",value);
}

/* Absolute layers pull the joints they key toward their pose; additive layers add on top. */
static void anim_pose_apply(XmlNode *proot,AnimPose *acc,const AnimPose *layer,float w,int additive,const char *mask){
	for(int i=0;i<layer->njoints;i++){
		const AnimJoint *lj=&layer->joints[i];
		if(!anim_masked(proot,mask,lj->name)) continue;
		AnimJoint *j=anim_joint(acc,lj->name);
		if(additive){ j->q=quat_mul(j->q,quat_slerp(quat_identity(),lj->q,w)); j->pos=vadd(j->pos,vscale(lj->pos,w)); }
		else { j->q=quat_slerp(j->q,lj->q,w); j->pos=lerp(j->pos,lj->pos,w); }
	}
	for(int i=0;i<layer->niks;i++){
		const AnimIk *li=&layer->iks[i];
		if(!anim_masked(proot,mask,li->tip)) continue;
		AnimIk *base=anim_ik_find(acc,li->tip);
		XmlNode *copy=xml_clone(li->ik,NULL);
		if(additive && base){
			vec3 offset=vadd(xml_attr_v3(base->ik,"offset",v3(0,0,0)),vscale(xml_attr_v3(copy,"offset",v3(0,0,0)),w));
			xml_set_attr_v3(base->ik,"offset",offset); xml_free(copy); continue;
		}
		if(base) anim_ik_mix(copy,base->ik,w); else anim_scale_weight(copy,w);
		anim_put_ik(acc,li->tip,copy);
	}
}

/* Full blend of two keyed poses: a joint missing from one side is at rest there. */
static void anim_pose_mix(XmlNode *proot,const AnimPose *a,const AnimPose *b,float u,AnimPose *out){
	for(int i=0;i<a->njoints;i++){
		const AnimJoint *bj=anim_joint_find(b,a->joints[i].name);
		AnimJoint *j=anim_joint(out,a->joints[i].name);
		j->q=quat_slerp(a->joints[i].q,bj?bj->q:quat_identity(),u);
		j->pos=lerp(a->joints[i].pos,bj?bj->pos:v3(0,0,0),u);
	}
	for(int i=0;i<b->njoints;i++) if(!anim_joint_find(a,b->joints[i].name)){
		AnimJoint *j=anim_joint(out,b->joints[i].name);
		j->q=quat_slerp(quat_identity(),b->joints[i].q,u); j->pos=vscale(b->joints[i].pos,u);
	}
	for(int i=0;i<a->niks;i++){
		const AnimIk *bi=anim_ik_find(b,a->iks[i].tip);
		XmlNode *copy=xml_clone(bi?bi->ik:a->iks[i].ik,NULL);
		if(bi) anim_ik_mix(copy,a->iks[i].ik,u); else anim_scale_weight(copy,1-u);
		anim_put_ik(out,a->iks[i].tip,copy);
	}
	for(int i=0;i<b->niks;i++) if(!anim_ik_find(a,b->iks[i].tip)){
		XmlNode *copy=xml_clone(b->iks[i].ik,NULL);
		anim_scale_weight(copy,u);
		anim_put_ik(out,b->iks[i].tip,copy);
	}
	(void)proot;
}

static XmlNode *rig_find_clip(Scene *s,XmlNode *proot,const char *name){
	XmlNode *containers[]={(XmlNode*)s->sceneRoot,proot};
	for(int c=0;c<2;c++) for(int i=0;containers[c] && i<containers[c]->nkids;i++)
		if(!strcmp(containers[c]->kids[i]->tag,"clip") && !strcmp(xml_attr(containers[c]->kids[i],"name",""),name)) return containers[c]->kids[i];
	return NULL;
}

/* A key is a named pose, its own joint and ik entries, or a pose refined by them. */
static void anim_key_read(Scene *s,XmlNode *proot,XmlNode *key,int mirror,AnimPose *out){
	const char *poseName=xml_attr(key,"pose",NULL);
	if(poseName){
		XmlNode *pose=rig_find_pose(s,proot,poseName);
		if(pose) anim_pose_read(s,proot,pose,mirror,out);
		else { fprintf(stderr,"[scener] clip key names unknown pose '%s'\n",poseName); fflush(stderr); }
	}
	AnimPose own={0};
	anim_pose_read(s,proot,key,mirror,&own);
	anim_pose_apply(proot,out,&own,1,0,NULL);
	anim_pose_free(&own);
}

static float anim_ease(XmlNode *clip,XmlNode *key,float u){
	const char *ease=xml_attr(key,"ease",xml_attr(clip,"ease","smooth"));
	if(!strcmp(ease,"linear")) return u;
	if(!strcmp(ease,"step")) return u>=1?1:0;
	if(strcmp(ease,"smooth")){ fprintf(stderr,"[scener] unknown ease '%s'; use smooth, linear or step\n",ease); fflush(stderr); }
	return motion_ease(u);
}

static void anim_clip_sample(Scene *s,XmlNode *proot,XmlNode *clip,float t,int mirror,AnimPose *out){
	XmlNode *keys[ANIM_MAX_KEYS]; int nkeys=0;
	for(int i=0;i<clip->nkids && nkeys<ANIM_MAX_KEYS;i++) if(!strcmp(clip->kids[i]->tag,"key")) keys[nkeys++]=clip->kids[i];
	if(!nkeys){ fprintf(stderr,"[scener] clip %s has no keys\n",xml_attr(clip,"name","?")); fflush(stderr); return; }
	float last=xml_attr_f(keys[nkeys-1],"t",0),length=xml_attr_f(clip,"length",last);
	int loop=xml_attr_i(clip,"loop",0);
	if(loop && length>0){ t=fmodf(t,length); if(t<0) t+=length; }
	int i=-1;
	for(int k=0;k<nkeys;k++) if(xml_attr_f(keys[k],"t",0)<=t) i=k;
	int next=i<0?0:i+1<nkeys?i+1:loop?0:-1;
	if(i<0 || next<0 || next==i){ anim_key_read(s,proot,keys[i<0?0:i],mirror,out); return; }
	float t0=xml_attr_f(keys[i],"t",0),t1=next>i?xml_attr_f(keys[next],"t",0):length+xml_attr_f(keys[next],"t",0);
	float u=t1>t0?anim_ease(clip,keys[next],(t-t0)/(t1-t0)):1;
	AnimPose a={0},b={0};
	anim_key_read(s,proot,keys[i],mirror,&a); anim_key_read(s,proot,keys[next],mirror,&b);
	anim_pose_mix(proot,&a,&b,u,out);
	anim_pose_free(&a); anim_pose_free(&b);
}

static float anim_layer_weight(XmlNode *layer,float time){
	float start=xml_attr_f(layer,"start",0),end=xml_attr_f(layer,"end",INFINITY);
	float fadeIn=xml_attr_f(layer,"fadeIn",0),fadeOut=xml_attr_f(layer,"fadeOut",0),w=xml_attr_f(layer,"weight",1);
	if(time<start || time>end) return 0;
	if(fadeIn>0) w*=motion_ease((time-start)/fadeIn);
	if(fadeOut>0 && isfinite(end)) w*=motion_ease((end-time)/fadeOut);
	return w;
}

static void rig_collect_limbs(XmlNode *n,const char *type,GaitLeg *out,int *count){
	if(xml_is_limb(n) && !strcmp(rig_limb_type(n),type) && *count<GAIT_MAX_LIMBS){ out[*count].limb=n; out[*count].phase=0; (*count)++; }
	for(int i=0;i<n->nkids;i++) rig_collect_limbs(n->kids[i],type,out,count);
}

static XmlNode *rig_root_bone(XmlNode *proot){
	for(int i=0;i<proot->nkids;i++) if(xml_is_bone(proot->kids[i])) return proot->kids[i];
	return NULL;
}

/* The arm swings from its first bone past the collarbone. */
static XmlNode *rig_limb_upper(XmlNode *limb){
	XmlNode *walk=limb;
	for(int guard=0;walk && guard<GAIT_MAX_LIMBS;guard++){
		XmlNode *next=NULL;
		for(int i=0;i<walk->nkids && !next;i++) if(xml_is_bone(walk->kids[i]) && !bone_at(walk->kids[i])) next=walk->kids[i];
		if(!next || strcmp(next->tag,"collarbone")) return next;
		walk=next;
	}
	return NULL;
}

static void anim_rotate(AnimPose *acc,XmlNode *joint,mat4 rotation){
	if(!joint || !xml_attr(joint,"name",NULL)) return;
	AnimJoint *j=anim_joint(acc,xml_attr(joint,"name",""));
	j->q=quat_mul(j->q,quat_from_mat4(rotation));
}

static mat4 capture_pose_world(Scene *s,XmlNode *node,XmlNode *root,const AnimPose *pose);

#define GAIT_ROLL_STANCE 0.08f

static float anim_foot_roll(const gait_params_t *p,float legPhase,float phase,float total,float degrees,float *pivot){
	float step=floorf(phase-legPhase+0.5f),landing=step+legPhase+0.5f;
	float takeoff=landing-p->swing,after=phase-(landing-1),pitch=0;
	*pivot=1;
	if(phase<takeoff){
		if(after<GAIT_ROLL_STANCE && landing>1){ *pivot=0; pitch=-degrees*(1-motion_ease(after/GAIT_ROLL_STANCE)); }
		else if(landing<=total) pitch=degrees*motion_ease((phase-takeoff+GAIT_ROLL_STANCE)/GAIT_ROLL_STANCE);
	}else if(landing<=total){
		float u=(phase-takeoff)/p->swing;
		*pivot=1-motion_ease(u); pitch=degrees*(1-2*motion_ease(u));
	}
	return pitch*gait_amplitude(p,phase,total);
}

/* <gait distance="300" start="1"/> walks the character forward: legs plant through IK,
   the pelvis bobs, sways and twists, the spine counter-twists and the arms swing. */
/* CATMotion equivalents: stride and speed (Max Stride Length, Max Step Time), direction and
   mode="spot" (Globals Direction, Walk On Spot / Walk On Line), limb phase (LimbPhases),
   footRoll (FootPlatform Pitch), armSwing/armBend/armOut (Arm Swing, Bend, CrossSwing),
   hipTwist/pelvisRoll/sway/bounce (Pelvis Twist, Roll, WeightShift, Lift). */
static void anim_gait(Scene *s,XmlNode *proot,XmlNode *gait,AnimPose *acc,vec3 *travel){
	GaitLeg legs[GAIT_MAX_LIMBS],arms[GAIT_MAX_LIMBS]; int nlegs=0,narms=0;
	rig_collect_limbs(proot,"leg",legs,&nlegs); rig_collect_limbs(proot,"arm",arms,&narms);
	XmlNode *root=rig_root_bone(proot);
	const char *mode=xml_attr(gait,"mode","line");
	int spot=!strcmp(mode,"spot");
	if(!spot && strcmp(mode,"line")){ fprintf(stderr,"[scener] gait mode '%s'; use line or spot\n",mode); fflush(stderr); }
	float distance=xml_attr_f(gait,"distance",spot?INFINITY:0);
	if(!nlegs || !root || distance<=0){
		fprintf(stderr,"[scener] gait needs leg limbs and a positive distance (legs=%d distance=%g)\n",nlegs,distance); fflush(stderr); return;
	}
	float legPhase[GAIT_MAX_LIMBS],hip=0,reach=0;
	for(int i=0;i<nlegs;i++){
		XmlNode *hub=legs[i].limb->parent,*end=rig_limb_end(legs[i].limb);
		legPhase[i]=xml_attr_f(legs[i].limb,"phase",(strncmp(xml_attr(legs[i].limb,"name",""),"right_",6)?0:0.5f)+(hub!=root?GAIT_FRONT_PHASE:0));
		vec3 top=mat4_xform_point(rig_rest_world(s,legs[i].limb,proot),v3(0,0,0));
		hip=fmaxf(hip,top.z*CM_PER_METRE);
		if(end) reach=fmaxf(reach,vlen(vsub(top,mat4_xform_point(rig_rest_world(s,end,proot),v3(0,0,0))))*CM_PER_METRE);
	}
	gait_params_t p;
	p.stride=xml_attr_f(gait,"stride",hip*GAIT_STRIDE_PER_HIP);
	p.speed=xml_attr_f(gait,"speed",p.stride*GAIT_DEFAULT_CADENCE);
	p.lift=xml_attr_f(gait,"lift",p.stride*GAIT_LIFT_PER_STRIDE);
	p.bounce=xml_attr_f(gait,"bounce",p.stride*GAIT_BOUNCE_PER_STRIDE);
	p.sway=xml_attr_f(gait,"sway",p.stride*GAIT_SWAY_PER_STRIDE);
	p.hipTwist=xml_attr_f(gait,"hipTwist",GAIT_DEFAULT_HIP_TWIST);
	p.spineTwist=xml_attr_f(gait,"spineTwist",GAIT_DEFAULT_SPINE_TWIST);
	p.armSwing=xml_attr_f(gait,"armSwing",GAIT_DEFAULT_ARM_SWING);
	p.swing=fmaxf(GAIT_MIN_SWING,fminf(GAIT_MAX_SWING,xml_attr_f(gait,"swing",GAIT_DEFAULT_SWING)));
	/* By default a foot's stance is centred under its hip: it lands as far ahead as it leaves behind. */
	p.lead=xml_attr_f(gait,"lead",(1-p.swing)*0.5f);
	p.ramp=fmaxf(0,xml_attr_f(gait,"ramp",GAIT_DEFAULT_RAMP));
	p.pelvisRoll=xml_attr_f(gait,"pelvisRoll",GAIT_DEFAULT_PELVIS_ROLL);
	float footRoll=xml_attr_f(gait,"footRoll",GAIT_DEFAULT_FOOT_ROLL),armBend=xml_attr_f(gait,"armBend",GAIT_DEFAULT_ARM_BEND);
	float armOut=xml_attr_f(gait,"armOut",GAIT_DEFAULT_ARM_OUT),heading=xml_attr_f(gait,"direction",0);
	if(p.stride<=0 || p.speed<=0){ fprintf(stderr,"[scener] gait needs positive stride and speed (%g, %g)\n",p.stride,p.speed); fflush(stderr); return; }
	if(spot && !isfinite(distance)) distance=p.speed*GAIT_SPOT_SECONDS;
	/* Walk along Direction degrees from the facing (90 = the character's left), facing unchanged. */
	vec3 way=bone_direction(heading,0);
	float total=gait_total_phase(&p,legPhase,nlegs,distance),elapsed=s->time-xml_attr_f(gait,"start",0);
	float phase=gait_phase_at(&p,total,elapsed),along=gait_travel(&p,phase,total,distance);
	if(!spot) *travel=vadd(*travel,vscale(way,along));
	/* Before it starts and once it ends a gait only places the body; poses and later gaits own the legs. */
	if(elapsed<=0 || elapsed>=gait_duration(&p,total)) return;
	/* Compass gait: the pelvis drops just enough for every foot target to stay within reach
	   of a nearly straight leg, which gives the natural dip at double support. */
	float usable=reach*GAIT_REACH_FRACTION,drop=0;
	for(int i=0;i<nlegs;i++){
		float forward,lift,swing;
		gait_foot(&p,legPhase[i],phase,total,distance,&forward,&lift,&swing);
		float ahead=fminf(fabsf(forward-along),usable);
		drop=fmaxf(drop,reach-lift-sqrtf(usable*usable-ahead*ahead));
		XmlNode *ik=xml_new("ik");
		xml_set_attr(ik,"limb",xml_attr(legs[i].limb,"name",""));
		xml_set_attr_v3(ik,"offset",vadd(vscale(way,forward-along),v3(0,0,lift)));
		anim_put_ik(acc,rig_ik_tip_name(proot,ik),ik);
		(void)swing;
	}
	gait_body_t body; gait_body(&p,phase,total,&body);
	drop=fmaxf(0,drop)+xml_attr_f(gait,"crouch",0)*gait_amplitude(&p,phase,total);
	AnimJoint *pelvis=anim_joint(acc,xml_attr(root,"name",""));
	pelvis->pos=vadd(pelvis->pos,vscale(v3(body.sway,0,body.bob-drop),1.0f/CM_PER_METRE));
	anim_rotate(acc,root,mat4_mul(mat4_rot_z(body.hipTwist),mat4_rot_y(body.pelvisRoll)));
	for(int i=0;i<root->nkids;i++) if(!strcmp(root->kids[i]->tag,"spine")){ anim_rotate(acc,root->kids[i],mat4_rot_z(body.spineTwist)); break; }
	float a=gait_amplitude(&p,phase,total);
	for(int i=0;i<narms;i++){
		int right=!strncmp(xml_attr(arms[i].limb,"name",""),"right_",6);
		float swingForward=right?-body.armSwing:body.armSwing;
		XmlNode *upper=rig_limb_upper(arms[i].limb),*fore=NULL;
		anim_rotate(acc,upper,mat4_mul(mat4_rot_x(swingForward),mat4_rot_y((right?armOut:-armOut)*a)));
		for(int k=0;upper && k<upper->nkids && !fore;k++) if(xml_is_bone(upper->kids[k]) && !bone_at(upper->kids[k])) fore=upper->kids[k];
		/* Elbows bend more as the arm swings forward. */
		anim_rotate(acc,fore,mat4_rot_x(-a*armBend-fmaxf(0,-swingForward)*GAIT_ELBOW_FOLLOW));
	}
	for(int i=0;i<nlegs;i++){
		XmlNode *end=rig_limb_end(legs[i].limb); if(!end) continue;
		const char *name=xml_attr(end,"name",""); AnimIk *entry=anim_ik_find(acc,name); if(!entry) continue;
		float pivotFraction,pitch=anim_foot_roll(&p,legPhase[i],phase,total,footRoll,&pivotFraction);
		vec3 rest=mat4_xform_point(rig_rest_world(s,end,proot),v3(0,0,0));
		BoneGeom foot=bone_geom(end); float length=foot.length;
		for(int k=0;k<end->nkids;k++) if(!strcmp(end->kids[k]->tag,"digit")) length=fmaxf(length,foot.length+xml_attr_f_cm(end->kids[k],"length",0));
		vec3 pivot=vscale(foot.dir,length*pivotFraction); pivot.z=-rest.z;
		mat4 turn=mat4_rot_x(pitch),parent=capture_pose_world(s,end->parent,proot,acc);
		anim_joint(acc,name)->q=quat_from_mat4(mat4_mul(mat4_affine_inverse(parent),turn));
		vec3 offset=xml_attr_v3(entry->ik,"offset",v3(0,0,0));
		offset=vadd(offset,vscale(vsub(pivot,mat4_xform_dir(turn,pivot)),CM_PER_METRE));
		xml_set_attr_v3(entry->ik,"offset",offset);
	}
	float extraDrop=0;
	for(int i=0;i<nlegs;i++){
		XmlNode *end=rig_limb_end(legs[i].limb); if(!end) continue;
		AnimIk *entry=anim_ik_find(acc,xml_attr(end,"name","")); if(!entry) continue;
		vec3 h=mat4_xform_point(capture_pose_world(s,end->parent->parent,proot,acc),v3(0,0,0));
		vec3 k=mat4_xform_point(capture_pose_world(s,end->parent,proot,acc),v3(0,0,0));
		vec3 f=mat4_xform_point(capture_pose_world(s,end,proot,acc),v3(0,0,0));
		vec3 goal=vadd(mat4_xform_point(rig_rest_world(s,end,proot),v3(0,0,0)),xml_attr_v3_cm(entry->ik,"offset",v3(0,0,0)));
		float length=(vlen(vsub(k,h))+vlen(vsub(f,k)))*GAIT_REACH_FRACTION;
		float lateral=(goal.x-h.x)*(goal.x-h.x)+(goal.y-h.y)*(goal.y-h.y);
		extraDrop=fmaxf(extraDrop,h.z-goal.z-sqrtf(fmaxf(0,length*length-lateral)));
	}
	anim_joint(acc,xml_attr(root,"name",""))->pos.z-=extraDrop;
}

#include "scene_capture.h"

static XmlNode *anim_pose_node(Scene *s,AnimPose *p){
	XmlNode *pose=xml_new("pose");
	xml_set_attr(pose,"name","_timeline");
	for(int i=0;i<p->njoints;i++){
		XmlNode *joint=xml_new("joint"); char value[128];
		joint->parent=pose;
		xml_set_attr(joint,"target",p->joints[i].name);
		snprintf(value,sizeof(value),"%.7g %.7g %.7g %.7g",p->joints[i].q.w,p->joints[i].q.x,p->joints[i].q.y,p->joints[i].q.z);
		xml_set_attr(joint,"_quat",value);
		xml_set_attr_v3_cm(joint,"pos",cvt3ds_inv(s,p->joints[i].pos));
		DA_PUSH(pose->kids,pose->nkids,pose->ckids,joint);
	}
	for(int i=0;i<p->niks;i++){
		p->iks[i].ik->parent=pose;
		DA_PUSH(pose->kids,pose->nkids,pose->ckids,p->iks[i].ik);
		p->iks[i].ik=NULL;
	}
	return pose;
}

static XmlNode *rig_timeline_pose(Scene *s,XmlNode *instance,XmlNode *proot,XmlNode *base,vec3 *travel){
	*travel=v3(0,0,0);
	int animated=0;
	for(int i=0;i<instance->nkids;i++) animated|=!strcmp(instance->kids[i]->tag,"layer") || !strcmp(instance->kids[i]->tag,"gait");
	if(!animated) return NULL;
	AnimPose acc={0};
	anim_pose_read(s,proot,base,0,&acc);
	for(int i=0;i<instance->nkids;i++){
		XmlNode *item=instance->kids[i];
		if(!strcmp(item->tag,"gait")){ anim_gait(s,proot,item,&acc,travel); continue; }
		if(strcmp(item->tag,"layer")) continue;
		float w=anim_layer_weight(item,s->time);
		if(w<=0) continue;
		const char *poseName=xml_attr(item,"pose",NULL),*clipName=xml_attr(item,"clip",NULL),*mode=xml_attr(item,"mode","absolute");
		int mirror=xml_attr_i(item,"mirror",0);
		AnimPose layer={0};
		if(poseName){
			XmlNode *pose=rig_find_pose(s,proot,poseName);
			if(pose) anim_pose_read(s,proot,pose,mirror,&layer);
			else { fprintf(stderr,"[scener] layer names unknown pose '%s'\n",poseName); fflush(stderr); }
		} else if(clipName){
			XmlNode *clip=rig_find_clip(s,proot,clipName);
			if(clip) anim_clip_sample(s,proot,clip,(s->time-xml_attr_f(item,"start",0))*xml_attr_f(item,"speed",1),mirror,&layer);
			else { fprintf(stderr,"[scener] layer names unknown clip '%s'\n",clipName); fflush(stderr); }
		} else if(xml_attr(item,"mocap",NULL)){
			mocap_retarget(s,proot,item,s->time-xml_attr_f(item,"start",0),&layer);
			if(mirror) anim_pose_mirror(proot,&layer);
		} else { fprintf(stderr,"[scener] layer needs pose, clip or mocap\n"); fflush(stderr); }
		/* CAT names additive layers "Adjustment" layers. */
		int additive=!strcmp(mode,"additive") || !strcmp(mode,"adjustment");
		if(!additive && strcmp(mode,"absolute")){ fprintf(stderr,"[scener] layer mode '%s'; use absolute, additive or adjustment\n",mode); fflush(stderr); }
		anim_pose_apply(proot,&acc,&layer,fminf(1,w),additive,xml_attr(item,"mask",NULL));
		anim_pose_free(&layer);
	}
	XmlNode *node=anim_pose_node(s,&acc);
	anim_pose_free(&acc);
	return node;
}

#endif
