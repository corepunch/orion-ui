#include "simplegl.h"
#include <ctype.h>
#include <stdio.h>

#define BVH_MAX_TOKEN 128
#define BVH_MAX_DEPTH 64
#define BVH_END_SUFFIX "_End"

/* BVH motion capture: a joint hierarchy with offsets and per-frame channels.
   End sites become joints named after their parent plus "_End", so a chain's
   last direction (head top, hand, toe) is available like any other joint. */

typedef struct { const char *p; } bvh_reader_t;

static int bvh_token(bvh_reader_t *r,char *out){
	while(*r->p && isspace((unsigned char)*r->p)) r->p++;
	if(!*r->p) return 0;
	int n=0;
	if(*r->p=='{' || *r->p=='}'){ out[0]=*r->p++; out[1]=0; return 1; }
	while(*r->p && !isspace((unsigned char)*r->p) && *r->p!='{' && *r->p!='}' && n<BVH_MAX_TOKEN-1) out[n++]=*r->p++;
	out[n]=0;
	return 1;
}

static int bvh_expect(bvh_reader_t *r,const char *word){
	char token[BVH_MAX_TOKEN];
	return bvh_token(r,token) && !strcmp(token,word);
}

static float bvh_float(bvh_reader_t *r,int *ok){
	char token[BVH_MAX_TOKEN],*end;
	if(!bvh_token(r,token)){ *ok=0; return 0; }
	float v=strtof(token,&end);
	if(*end) *ok=0;
	return v;
}

static int bvh_channel(const char *name){
	static const char *names[]={"Xposition","Yposition","Zposition","Xrotation","Yrotation","Zrotation"};
	for(int i=0;i<BVH_CHANNEL_KINDS;i++) if(!strcmp(name,names[i])) return i;
	return -1;
}

static int bvh_add_joint(bvh_clip_t *c,const char *name,int parent){
	bvh_joint_t j; memset(&j,0,sizeof(j));
	snprintf(j.name,sizeof(j.name),"%s",name);
	j.parent=parent;
	DA_PUSH(c->joints,c->njoints,c->cjoints,j);
	return c->njoints-1;
}

static int bvh_parse_joint(bvh_reader_t *r,bvh_clip_t *c,const char *name,int parent,int depth){
	char token[BVH_MAX_TOKEN];
	if(depth>BVH_MAX_DEPTH || !bvh_expect(r,"{")) return 0;
	int index=bvh_add_joint(c,name,parent),ok=1;
	while(bvh_token(r,token)){
		if(!strcmp(token,"}")) return 1;
		if(!strcmp(token,"OFFSET")){
			vec3 o; o.x=bvh_float(r,&ok); o.y=bvh_float(r,&ok); o.z=bvh_float(r,&ok);
			c->joints[index].offset=o;
		} else if(!strcmp(token,"CHANNELS")){
			int count=(int)bvh_float(r,&ok);
			if(count<0 || count>BVH_MAX_CHANNELS) return 0;
			c->joints[index].nchannels=count; c->joints[index].firstChannel=c->nchannels;
			for(int i=0;i<count;i++){
				if(!bvh_token(r,token)) return 0;
				c->joints[index].channels[i]=bvh_channel(token);
				if(c->joints[index].channels[i]<0) return 0;
			}
			c->nchannels+=count;
		} else if(!strcmp(token,"JOINT")){
			char child[BVH_MAX_TOKEN];
			if(!bvh_token(r,child) || !bvh_parse_joint(r,c,child,index,depth+1)) return 0;
		} else if(!strcmp(token,"End")){
			char end[sizeof(c->joints[0].name)+sizeof(BVH_END_SUFFIX)];
			if(!bvh_token(r,token) || !bvh_expect(r,"{") || !bvh_expect(r,"OFFSET")) return 0;
			snprintf(end,sizeof(end),"%s" BVH_END_SUFFIX,c->joints[index].name);
			int site=bvh_add_joint(c,end,index);
			vec3 o; o.x=bvh_float(r,&ok); o.y=bvh_float(r,&ok); o.z=bvh_float(r,&ok);
			c->joints[site].offset=o;
			if(!bvh_expect(r,"}")) return 0;
		} else return 0;
		if(!ok) return 0;
	}
	return 0;
}

static char *bvh_read_text(const char *path){
	FILE *f=fopen(path,"rb");
	if(!f) return NULL;
	fseek(f,0,SEEK_END); long size=ftell(f); fseek(f,0,SEEK_SET);
	char *text=size>=0?malloc((size_t)size+1):NULL;
	size_t got=text?fread(text,1,(size_t)size,f):0;
	fclose(f);
	if(!text || got!=(size_t)size){ free(text); return NULL; }
	text[size]=0;
	return text;
}

bvh_clip_t *bvh_load(const char *path){
	char *text=bvh_read_text(path);
	if(!text){ fprintf(stderr,"[bvh] cannot read %s\n",path); fflush(stderr); return NULL; }
	bvh_clip_t *c=calloc(1,sizeof(bvh_clip_t));
	bvh_reader_t r={text};
	char token[BVH_MAX_TOKEN],name[BVH_MAX_TOKEN];
	int ok=bvh_expect(&r,"HIERARCHY") && bvh_expect(&r,"ROOT") && bvh_token(&r,name) && bvh_parse_joint(&r,c,name,-1,0);
	ok=ok && bvh_expect(&r,"MOTION") && bvh_expect(&r,"Frames:");
	if(ok){ c->nframes=(int)bvh_float(&r,&ok); ok=ok && bvh_expect(&r,"Frame") && bvh_expect(&r,"Time:"); }
	if(ok) c->frameTime=bvh_float(&r,&ok);
	ok=ok && c->nframes>0 && c->frameTime>0 && c->nchannels>0;
	if(ok){
		size_t count=(size_t)c->nframes*(size_t)c->nchannels;
		c->data=malloc(count*sizeof(float));
		for(size_t i=0;ok && i<count;i++){
			if(!bvh_token(&r,token)){ ok=0; break; }
			char *end; c->data[i]=strtof(token,&end); ok=!*end;
		}
	}
	free(text);
	if(!ok){
		fprintf(stderr,"[bvh] %s: malformed file (joints=%d channels=%d frames=%d)\n",path,c->njoints,c->nchannels,c->nframes); fflush(stderr);
		bvh_free(c); return NULL;
	}
	return c;
}

void bvh_free(bvh_clip_t *c){
	if(!c) return;
	free(c->joints); free(c->data); free(c);
}

int bvh_find(const bvh_clip_t *c,const char *name){
	for(int i=0;c && i<c->njoints;i++) if(!strcmp(c->joints[i].name,name)) return i;
	return -1;
}

float bvh_duration(const bvh_clip_t *c){ return c?(c->nframes-1)*c->frameTime:0; }

static void bvh_local(const bvh_clip_t *c,int joint,int frame,vec3 *pos,quat *rot){
	const bvh_joint_t *j=&c->joints[joint];
	const float *v=c->data+(size_t)frame*(size_t)c->nchannels+j->firstChannel;
	mat4 r=mat4_identity();
	*pos=j->offset;
	for(int i=0;i<j->nchannels;i++) switch(j->channels[i]){
		case 0: pos->x+=v[i]; break;
		case 1: pos->y+=v[i]; break;
		case 2: pos->z+=v[i]; break;
		case 3: r=mat4_mul(r,mat4_rot_x(v[i])); break;
		case 4: r=mat4_mul(r,mat4_rot_y(v[i])); break;
		case 5: r=mat4_mul(r,mat4_rot_z(v[i])); break;
	}
	*rot=quat_from_mat4(r);
}

/* World positions and rotations of every joint at `seconds`, blending neighbouring frames. */
void bvh_evaluate(const bvh_clip_t *c,float seconds,vec3 *pos,quat *rot){
	float f=fmaxf(0,fminf((float)(c->nframes-1),seconds/c->frameTime));
	int f0=(int)floorf(f),f1=f0+1<c->nframes?f0+1:f0;
	float u=f-f0;
	for(int i=0;i<c->njoints;i++){
		vec3 p0,p1; quat q0,q1;
		bvh_local(c,i,f0,&p0,&q0); bvh_local(c,i,f1,&p1,&q1);
		vec3 p=lerp(p0,p1,u); quat q=quat_slerp(q0,q1,u);
		int parent=c->joints[i].parent;
		if(parent<0){ pos[i]=p; rot[i]=q; continue; }
		pos[i]=vadd(pos[parent],mat4_xform_dir(mat4_from_quat(rot[parent]),p));
		rot[i]=quat_mul(rot[parent],q);
	}
}

void bvh_reference(const bvh_clip_t *c,int frame,vec3 *pos,quat *rot){
	if(frame>=0){ bvh_evaluate(c,frame*c->frameTime,pos,rot); return; }
	for(int i=0;i<c->njoints;i++){
		int parent=c->joints[i].parent;
		pos[i]=parent<0?c->joints[i].offset:vadd(pos[parent],c->joints[i].offset);
		rot[i]=quat_identity();
	}
}
