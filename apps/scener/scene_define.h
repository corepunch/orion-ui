#ifndef __SCENE_DEFINE_H__
#define __SCENE_DEFINE_H__

/* Utilities written in XML. A <define name="soft-box" doc="..."> declares <param name default doc>
   children and one body element. A later <soft-box .../> is replaced by a copy of that body with
   ${param} substituted in every attribute; attributes that are not params (pos, rot, color,
   material, ...) go onto the body element, and child elements of the call are appended to it.
   Definitions sit at the top of the scene or in a <library> file pulled in by <include src>.
   The engine gains no primitive: a utility is only a named, documented composition of the atoms. */

#define SCENE_DEFINE_MAX_EXPANSIONS 20000
#define SCENE_DEFINE_MAX_INCLUDE_DEPTH 8

typedef struct { XmlNode **defs; int ndefs,cdefs; XmlNode **owned; int nowned,cowned; int expansions; } scene_defines_t;

static XmlNode *scene_define_find(scene_defines_t *d,const char *tag){
	for(int i=0;i<d->ndefs;i++) if(!strcmp(xml_attr(d->defs[i],"name",""),tag)) return d->defs[i];
	return NULL;
}

static void scene_define_collect(Scene *s,scene_defines_t *d,XmlNode *root,const char *path,int depth){
	for(int i=0;i<root->nkids;){
		XmlNode *k=root->kids[i];
		int isDef=!strcmp(k->tag,"define"),isInc=!strcmp(k->tag,"include");
		if(!isDef && !isInc){ i++; continue; }
		memmove(root->kids+i,root->kids+i+1,(size_t)(root->nkids-i-1)*sizeof(*root->kids)); root->nkids--;
		DA_PUSH(d->owned,d->nowned,d->cowned,k);
		if(isDef){
			const char *name=xml_attr(k,"name",NULL);
			if(!name || !scene_let_name_valid_tag(name)){ scene_issue(s,SCENE_ISSUE_ERROR,"define",path,k->line,"<define> needs a name (letters, digits, '-' and '_')"); continue; }
			if(scene_define_find(d,name)) scene_issue(s,SCENE_ISSUE_WARNING,"define",path,k->line,"<define name=\"%s\"> replaces an earlier definition",name);
			DA_PUSH(d->defs,d->ndefs,d->cdefs,k);
			continue;
		}
		const char *src=xml_attr(k,"src",NULL);
		if(!src || depth>=SCENE_DEFINE_MAX_INCLUDE_DEPTH){ scene_issue(s,SCENE_ISSUE_ERROR,"include",path,k->line,src?"<include> nests too deeply":"<include> needs src"); continue; }
		char full[SCENE_PREFAB_PATH_CAPACITY]; const char *slash=strrchr(path,'/');
		if(slash) snprintf(full,sizeof(full),"%.*s/%s",(int)(slash-path),path,src); else snprintf(full,sizeof(full),"%s",src);
		char *buf=read_file(full);
		if(!buf){ scene_issue(s,SCENE_ISSUE_ERROR,"include",path,k->line,"cannot read %s",full); continue; }
		XmlNode *lib=xml_parse(buf); free(buf);
		if(!lib){ scene_issue(s,SCENE_ISSUE_ERROR,"include",full,0,"cannot parse library"); continue; }
		DA_PUSH(d->owned,d->nowned,d->cowned,lib);
		scene_define_collect(s,d,lib,full,depth+1);
	}
}

static char *scene_define_substitute(Scene *s,XmlNode *call,XmlNode *def,const char *path,const char *text,char names[][XML_NAME_CAPACITY],char **values,int n){
	size_t cap=strlen(text)+1,len=0; char *out=malloc(cap); int depth=0;
	for(const char *p=text;*p;){
		if(p[0]!='$' || p[1]!='{'){ if(*p=='{') depth++; else if(*p=='}') depth--; out[len++]=*p++; continue; }
		const char *end=strchr(p,'}'); if(!end){ out[len++]=*p++; continue; }
		size_t nl=(size_t)(end-p-2); int hit=-1;
		for(int i=0;i<n;i++) if(strlen(names[i])==nl && !strncmp(names[i],p+2,nl)) hit=i;
		if(hit<0){ scene_issue(s,SCENE_ISSUE_ERROR,"define",path,call->line,"<%s>: body of <define name=\"%s\"> uses unknown parameter ${%.*s}",call->tag,xml_attr(def,"name",""),(int)nl,p+2); p=end+1; continue; }
		const char *val=values[hit]; size_t vl=strlen(val);
		/* an argument that is itself {expr}, used inside another expression, becomes (expr) */
		if(depth>0 && vl>=2 && val[0]=='{' && val[vl-1]=='}'){
			char wrapped[XML_VALUE_CAPACITY]; snprintf(wrapped,sizeof(wrapped),"(%.*s)",(int)vl-2,val+1);
			cap+=vl; out=realloc(out,cap); memcpy(out+len,wrapped,vl); len+=vl; p=end+1; continue;
		}
		cap+=vl; out=realloc(out,cap); memcpy(out+len,val,vl); len+=vl; p=end+1;
	}
	out[len]=0; return out;
}

static void scene_define_apply(Scene *s,XmlNode *n,XmlNode *call,XmlNode *def,const char *path,char names[][XML_NAME_CAPACITY],char **values,int np){
	n->line=call->line;
	for(int i=0;i<n->nattrs;i++){
		char *v=scene_define_substitute(s,call,def,path,n->attrs[i].value,names,values,np);
		free(n->attrs[i].value); n->attrs[i].value=v;
	}
	for(int i=0;i<n->nkids;i++) scene_define_apply(s,n->kids[i],call,def,path,names,values,np);
}

/* Replaces kids[index] of parent by the expansion of its define; returns 0 if the call failed (the node is dropped). */
static void scene_define_expand_call(Scene *s,scene_defines_t *d,XmlNode *parent,int index,XmlNode *def,const char *path){
	XmlNode *call=parent->kids[index];
	char names[32][XML_NAME_CAPACITY]; char *values[32]; int np=0,body=-1,bad=0;
	for(int i=0;i<def->nkids;i++){
		XmlNode *k=def->kids[i];
		if(!strcmp(k->tag,"param")){
			const char *pn=xml_attr(k,"name",NULL); if(!pn || np>=32) continue;
			snprintf(names[np],XML_NAME_CAPACITY,"%s",pn);
			const char *given=xml_attr(call,pn,NULL),*dflt=xml_attr(k,"default",NULL);
			if(!given && !dflt){ scene_issue(s,SCENE_ISSUE_ERROR,"define",path,call->line,"<%s> needs %s=\"...\"%s%s",call->tag,pn,xml_attr(k,"doc",NULL)?": ":"",xml_attr(k,"doc","")); bad=1; given=""; }
			values[np]=given?strdup(given):scene_define_substitute(s,call,def,path,dflt,names,values,np); np++;
		} else if(body<0) body=i;
		else { scene_issue(s,SCENE_ISSUE_ERROR,"define",path,def->line,"<define name=\"%s\"> must have exactly one body element",call->tag); bad=1; break; }
	}
	XmlNode *copy=NULL;
	if(body<0 && !bad){ scene_issue(s,SCENE_ISSUE_ERROR,"define",path,def->line,"<define name=\"%s\"> has no body element",call->tag); bad=1; }
	if(!bad){
		copy=xml_clone(def->kids[body],parent);
		scene_define_apply(s,copy,call,def,path,names,values,np);
		for(int i=0;i<call->nattrs;i++){
			int isParam=0; for(int j=0;j<np;j++) if(!strcmp(names[j],call->attrs[i].name)) isParam=1;
			if(!isParam) xml_set_attr(copy,call->attrs[i].name,call->attrs[i].value);
		}
		for(int i=0;i<call->nkids;i++){ XmlNode *k=call->kids[i]; call->kids[i]=NULL; k->parent=copy; DA_PUSH(copy->kids,copy->nkids,copy->ckids,k); }
		call->nkids=0;
	}
	for(int i=0;i<np;i++) free(values[i]);
	xml_free(call);
	if(copy) parent->kids[index]=copy;
	else { memmove(parent->kids+index,parent->kids+index+1,(size_t)(parent->nkids-index-1)*sizeof(*parent->kids)); parent->nkids--; }
}

static void scene_define_expand(Scene *s,scene_defines_t *d,XmlNode *n,const char *path){
	for(int i=0;i<n->nkids;){
		XmlNode *def=scene_define_find(d,n->kids[i]->tag);
		if(!def){ scene_define_expand(s,d,n->kids[i],path); i++; continue; }
		if(++d->expansions>SCENE_DEFINE_MAX_EXPANSIONS){
			scene_issue(s,SCENE_ISSUE_ERROR,"define",path,n->kids[i]->line,"too many utility expansions: <%s> calls itself",n->kids[i]->tag);
			XmlNode *drop=n->kids[i]; memmove(n->kids+i,n->kids+i+1,(size_t)(n->nkids-i-1)*sizeof(*n->kids)); n->nkids--; xml_free(drop); continue;
		}
		scene_define_expand_call(s,d,n,i,def,path);
		/* the same index is looked at again: the body may itself be a utility call */
	}
}

static void scene_expand_defines(Scene *s,XmlNode *root,const char *path){
	scene_defines_t d; memset(&d,0,sizeof(d));
	scene_define_collect(s,&d,root,path,0);
	if(d.ndefs) scene_define_expand(s,&d,root,path);
	for(int i=0;i<d.nowned;i++) xml_free(d.owned[i]);
	free(d.owned); free(d.defs);
}

#endif
