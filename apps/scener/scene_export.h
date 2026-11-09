#ifndef __SCENE_EXPORT_H__
#define __SCENE_EXPORT_H__

/* scener --export: hands the built scene to an exporter plugin as plain arrays.
   Geometry leaves in metres, Y up; a <scene up="z"> scene is turned upright first. */

#include "exporter_registry.h"

static void export_log(const char *message){ fprintf(stderr,"export: %s\n",message); }

typedef struct { scener_export_object_t *objects; char (*names)[96]; float *data; uint32_t *indices; } export_snapshot_t;

static void export_snapshot_free(export_snapshot_t *x){ free(x->objects); free(x->names); free(x->data); free(x->indices); memset(x,0,sizeof(*x)); }

static vec3 export_upright(const Scene *s,vec3 v){ return s->worldUp.z==1?v3(v.x,v.z,-v.y):v; }

/* One snapshot object per renderable node mesh; names fall back to "<tag>_<index>". */
static bool export_snapshot_build(Scene *s,export_snapshot_t *x,scener_export_scene_t *out,float time){
  memset(x,0,sizeof(*x)); memset(out,0,sizeof(*out));
  int count=0; size_t nverts=0,ntris=0;
  for(int i=0;i<s->nobjs;i++){ SceneObj *o=&s->objs[i]; if(!o->renderable||!o->mesh.nverts||!o->mesh.ntris) continue; count++; nverts+=o->mesh.nverts; ntris+=o->mesh.ntris; }
  if(!count){ fprintf(stderr,"export: %s has no renderable geometry\n",s->scenePath); return false; }
  x->objects=calloc(count,sizeof(*x->objects)); x->names=calloc(count,sizeof(*x->names));
  x->data=malloc(nverts*8*sizeof(float)); x->indices=malloc(ntris*3*sizeof(uint32_t));
  if(!x->objects||!x->names||!x->data||!x->indices){ fprintf(stderr,"export: out of memory for %zu vertices\n",nverts); export_snapshot_free(x); return false; }
  vec3 lo=v3(1e30f,1e30f,1e30f),hi=v3(-1e30f,-1e30f,-1e30f);
  float *d=x->data; uint32_t *ix=x->indices; int n=0;
  for(int i=0;i<s->nobjs;i++){
    SceneObj *o=&s->objs[i]; Mesh *m=&o->mesh;
    if(!o->renderable||!m->nverts||!m->ntris) continue;
    XmlNode *node=(XmlNode*)o->sourceNode; const char *name=node?xml_peek(node,"name"):NULL;
    if(name&&*name) snprintf(x->names[n],sizeof(x->names[n]),"%s",name); else snprintf(x->names[n],sizeof(x->names[n]),"%s_%d",node?node->tag:"mesh",i);
    for(char *c=x->names[n];*c;c++) if(isspace((unsigned char)*c)) *c='_';
    scener_export_object_t *e=&x->objects[n++];
    e->name=x->names[n-1]; e->color[0]=o->color.x; e->color[1]=o->color.y; e->color[2]=o->color.z; e->shininess=o->shininess;
    e->nverts=m->nverts; e->ntris=m->ntris;
    e->positions=d; e->normals=d+3*m->nverts; e->uvs=d+6*m->nverts; d+=8*m->nverts;
    for(int v=0;v<m->nverts;v++){
      vec3 p=export_upright(s,m->verts[v].pos),nr=export_upright(s,m->verts[v].nrm);
      float *P=(float*)e->positions+3*v,*N=(float*)e->normals+3*v,*T=(float*)e->uvs+2*v;
      P[0]=p.x;P[1]=p.y;P[2]=p.z; N[0]=nr.x;N[1]=nr.y;N[2]=nr.z; T[0]=m->verts[v].u;T[1]=m->verts[v].v;
      lo=v3(fminf(lo.x,p.x),fminf(lo.y,p.y),fminf(lo.z,p.z)); hi=v3(fmaxf(hi.x,p.x),fmaxf(hi.y,p.y),fmaxf(hi.z,p.z));
    }
    e->indices=ix;
    for(int t=0;t<m->ntris;t++){ *ix++=(uint32_t)m->tris[t].a; *ix++=(uint32_t)m->tris[t].b; *ix++=(uint32_t)m->tris[t].c; }
  }
  out->abi=SCENER_EXPORTER_ABI; out->source=s->scenePath; out->authored_up=s->worldUp.z==1?'z':'y'; out->time=time;
  out->bounds_min[0]=lo.x;out->bounds_min[1]=lo.y;out->bounds_min[2]=lo.z; out->bounds_max[0]=hi.x;out->bounds_max[1]=hi.y;out->bounds_max[2]=hi.z;
  out->nobjects=count; out->objects=x->objects; out->log=export_log;
  return true;
}

void export_list(FILE *f){
  exporter_registry_load();
  if(!exporter_registry_count()){ fprintf(f,"no exporters found (searched SCENER_EXPORTER_PATH, the library and settings exporters directories)\n"); return; }
  for(int i=0;i<exporter_registry_count();i++){
    const char *path=""; const scener_exporter_t *x=exporter_registry_at(i,&path);
    fprintf(f,"%s\t",x->id); for(const char *const *e=x->extensions;*e;e++) fprintf(f,"%s%s",e==x->extensions?"":",",*e);
    fprintf(f,"\t%s\t%s\n",x->short_desc,path);
    for(size_t o=0;o<x->noptions;o++) fprintf(f,"  --opt %s=%s\t%s (default %s)\n",x->options[o].id,x->options[o].choices?x->options[o].choices:"0|1",x->options[o].label,x->options[o].default_value?x->options[o].default_value:"");
  }
}

/* keys/values are --opt pairs; every key must be declared by the exporter. */
bool scene_export_file(Scene *s,const char *path,const char *id,const char *const *keys,const char *const *values,int nopts,float time){
  exporter_registry_load();
  const scener_exporter_t *x=id&&*id?exporter_registry_find(id):exporter_registry_for_file(path);
  if(!x){ if(id&&*id) fprintf(stderr,"export: no exporter \"%s\"; run --list-exporters\n",id); else fprintf(stderr,"export: no exporter handles %s; run --list-exporters\n",path); return false; }
  for(int i=0;i<nopts;i++){
    size_t o=0; while(o<x->noptions&&strcmp(x->options[o].id,keys[i])) o++;
    if(o==x->noptions){ fprintf(stderr,"export: exporter %s has no option \"%s\"; run --list-exporters\n",x->id,keys[i]); return false; }
    const char *choices=x->options[o].choices; size_t len=strlen(values[i]); bool ok=false;
    if(!choices) ok=!strcmp(values[i],"0")||!strcmp(values[i],"1");
    else for(const char *c=choices;*c&&!ok;){ const char *e=strchr(c,','); size_t n=e?(size_t)(e-c):strlen(c); ok=n==len&&!strncmp(c,values[i],n); c=e?e+1:c+n; }
    if(!ok){ fprintf(stderr,"export: option %s=%s is not valid (%s)\n",keys[i],values[i],choices?choices:"0 or 1"); return false; }
  }
  export_snapshot_t snap; scener_export_scene_t scene;
  if(!export_snapshot_build(s,&snap,&scene,time)) return false;
  scener_export_options_t options={ (size_t)nopts,keys,values };
  bool ok=x->export_scene(path,&scene,&options);
  if(!ok) fprintf(stderr,"export: exporter %s failed writing %s\n",x->id,path);
  export_snapshot_free(&snap);
  return ok;
}

#endif
