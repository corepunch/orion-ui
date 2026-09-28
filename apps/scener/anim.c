#include "simplegl.h"
#include <ctype.h>
#include <stdio.h>

/* BVH motion capture: hierarchy offsets plus per-frame Euler channels. Scener
   only reads joint world positions from it; retargeting aims the character's
   own bones along the captured segments, so rest poses and proportions may differ. */

#define BVH_TOKEN_CAPACITY 128
#define BVH_MAX_DEPTH 64
#define BVH_ANGLE_WRAP 180.0f
#define BVH_FULL_TURN 360.0f

enum { BVH_XPOS, BVH_YPOS, BVH_ZPOS, BVH_XROT, BVH_YROT, BVH_ZROT };

typedef struct { const char *p; } bvh_reader_t;

static int bvh_token(bvh_reader_t *r,char *out,size_t size){
	while(*r->p && isspace((unsigned char)*r->p)) r->p++;
	if(!*r->p) return 0;
	size_t n=0;
	while(*r->p && !isspace((unsigned char)*r->p)){ if(n+1<size) out[n++]=*r->p; r->p++; }
	out[n]=0;
	return 1;
}

static int bvh_expect(bvh_reader_t *r,const char *word){
	char token[BVH_TOKEN_CAPACITY];
	if(!bvh_token(r,token,sizeof(token)) || strcmp(token,word)){
		fprintf(stderr,"[bvh] expected '%s', found '%s'\n",word,token); fflush(stderr); return 0;
	}
	return 1;
}

static int bvh_float(bvh_reader_t *r,float *out){
	char token[BVH_TOKEN_CAPACITY],*end;
	if(!bvh_token(r,token,sizeof(token))) return 0;
	*out=strtof(token,&end);
	return !*end;
}

static int bvh_channel(const char *name){
	static const char *names[]={"Xposition","Yposition","Zposition","Xrotation","Yrotation","Zrotation"};
	for(int i=0;i<6;i++) if(!strcmp(name,names[i])) return i;
	return -1;
}

static int bvh_parse_joint(bvh_reader_t *r,BvhClip *clip,int parent,const char *name,int depth){
	if(depth>=BVH_MAX_DEPTH){ fprintf(stderr,"[bvh] hierarchy deeper than %d\n",BVH_MAX_DEPTH); return 0; }
	BvhJoint joint; memset(&joint,0,sizeof(joint));
	snprintf(joint.name,sizeof(joint.name),"%s",name);
	joint.parent=parent; joint.channelStart=clip->nchannels;
	int index=clip->njoints;
	DA_PUSH(clip->joints,clip->njoints,clip->cjoints,joint);
	char token[BVH_TOKEN_CAPACITY];
	if(!bvh_expect(r,"{")) return 0;
	while(bvh_token(r,token,sizeof(token))){
		BvhJoint *j=&clip->joints[index];
		if(!strcmp(token,"}")) return 1;
		if(!strcmp(token,"OFFSET")){
			if(!bvh_float(r,&j->offset.x) || !bvh_float(r,&j->offset.y) || !bvh_float(r,&j->offset.z)) return 0;
		} else if(!strcmp(token,"CHANNELS")){
			float count; if(!bvh_float(r,&count) || count<0 || count>6){ fprintf(stderr,"[bvh] %s: invalid channel count\n",j->name); return 0; }
			j->nchannels=(int)count;
			for(int c=0;c<j->nchannels;c++){
				if(!bvh_token(r,token,sizeof(token)) || (j->channels[c]=bvh_channel(token))<0){ fprintf(stderr,"[bvh] %s: unknown channel '%s'\n",j->name,token); return 0; }
			}
			clip->nchannels+=j->nchannels;
		} else if(!strcmp(token,"JOINT")){
			char child[BVH_TOKEN_CAPACITY];
			if(!bvh_token(r,child,sizeof(child)) || !bvh_parse_joint(r,clip,index,child,depth+1)) return 0;
		} else if(!strcmp(token,"End")){
			if(!bvh_expect(r,"Site") || !bvh_expect(r,"{") || !bvh_expect(r,"OFFSET")) return 0;
			if(!bvh_float(r,&j->endSite.x) || !bvh_float(r,&j->endSite.y) || !bvh_float(r,&j->endSite.z) || !bvh_expect(r,"}")) return 0;
			j->hasEndSite=1;
		} else { fprintf(stderr,"[bvh] %s: unexpected '%s'\n",j->name,token); return 0; }
	}
	fprintf(stderr,"[bvh] unterminated joint %s\n",name);
	return 0;
}

int bvh_load(const char *path,BvhClip *clip){
	memset(clip,0,sizeof(*clip));
	snprintf(clip->path,sizeof(clip->path),"%s",path);
	FILE *f=fopen(path,"rb");
	if(!f){ fprintf(stderr,"[bvh] cannot open %s\n",path); fflush(stderr); return 0; }
	fseek(f,0,SEEK_END); long size=ftell(f); fseek(f,0,SEEK_SET);
	char *text=malloc((size_t)size+1);
	size_t got=fread(text,1,(size_t)size,f); text[got]=0; fclose(f);
	bvh_reader_t r={text};
	char name[BVH_TOKEN_CAPACITY];
	float frames=0;
	int ok=bvh_expect(&r,"HIERARCHY") && bvh_expect(&r,"ROOT") && bvh_token(&r,name,sizeof(name)) &&
		bvh_parse_joint(&r,clip,-1,name,0) && bvh_expect(&r,"MOTION") && bvh_expect(&r,"Frames:") &&
		bvh_float(&r,&frames) && bvh_expect(&r,"Frame") && bvh_expect(&r,"Time:") && bvh_float(&r,&clip->frameTime);
	if(ok && (frames<1 || clip->frameTime<=0 || !clip->nchannels)){ fprintf(stderr,"[bvh] %s: empty motion\n",path); ok=0; }
	if(ok){
		clip->nframes=(int)frames;
		clip->values=malloc((size_t)clip->nframes*(size_t)clip->nchannels*sizeof(float));
		for(int i=0;ok && i<clip->nframes*clip->nchannels;i++) ok=bvh_float(&r,&clip->values[i]);
		if(!ok) fprintf(stderr,"[bvh] %s: motion data ends before %d frames\n",path,clip->nframes);
	}
	free(text);
	if(!ok){ fflush(stderr); bvh_free(clip); return 0; }
	fprintf(stderr,"[bvh] loaded %s joints=%d channels=%d frames=%d fps=%.1f\n",path,clip->njoints,clip->nchannels,clip->nframes,1.0f/clip->frameTime);
	return 1;
}

void bvh_free(BvhClip *clip){
	free(clip->joints); free(clip->values);
	memset(clip,0,sizeof(*clip));
}

/* Rig tools prefix joint names ("mixamorig:Hips"); match on the bare name. */
int bvh_find(const BvhClip *clip,const char *name){
	for(int i=0;i<clip->njoints;i++){
		const char *bare=strrchr(clip->joints[i].name,':');
		bare=bare?bare+1:clip->joints[i].name;
		if(!strcmp(bare,name)) return i;
	}
	return -1;
}

float bvh_duration(const BvhClip *clip){ return clip->nframes*clip->frameTime; }

static float bvh_channel_value(const BvhClip *clip,int channel,int kind,int f0,int f1,float u){
	float a=clip->values[(size_t)f0*clip->nchannels+channel],b=clip->values[(size_t)f1*clip->nchannels+channel];
	if(kind>=BVH_XROT){
		float d=fmodf(b-a+BVH_ANGLE_WRAP,BVH_FULL_TURN);
		if(d<0) d+=BVH_FULL_TURN;
		return a+(d-BVH_ANGLE_WRAP)*u;
	}
	return a+(b-a)*u;
}

void bvh_sample(const BvhClip *clip,float time,int loop,vec3 *joints,vec3 *ends){
	float frame=time/clip->frameTime;
	if(loop) frame=fmodf(frame,(float)clip->nframes),frame=frame<0?frame+clip->nframes:frame;
	else frame=fmaxf(0,fminf(frame,(float)(clip->nframes-1)));
	int f0=(int)frame,f1=loop?(f0+1)%clip->nframes:(f0+1<clip->nframes?f0+1:f0);
	float u=frame-f0;
	mat4 *world=malloc((size_t)clip->njoints*sizeof(mat4));
	for(int i=0;i<clip->njoints;i++){
		const BvhJoint *j=&clip->joints[i];
		vec3 pos=j->offset;
		mat4 rotation=mat4_identity();
		for(int c=0;c<j->nchannels;c++){
			float v=bvh_channel_value(clip,j->channelStart+c,j->channels[c],f0,f1,u);
			switch(j->channels[c]){
			case BVH_XPOS: pos.x=v; break;
			case BVH_YPOS: pos.y=v; break;
			case BVH_ZPOS: pos.z=v; break;
			case BVH_XROT: rotation=mat4_mul(rotation,mat4_rot_x(v)); break;
			case BVH_YROT: rotation=mat4_mul(rotation,mat4_rot_y(v)); break;
			case BVH_ZROT: rotation=mat4_mul(rotation,mat4_rot_z(v)); break;
			}
		}
		mat4 local=mat4_mul(mat4_translate(pos),rotation);
		world[i]=j->parent>=0?mat4_mul(world[j->parent],local):local;
		joints[i]=mat4_xform_point(world[i],v3(0,0,0));
		if(ends) ends[i]=j->hasEndSite?mat4_xform_point(world[i],j->endSite):joints[i];
	}
	free(world);
}
