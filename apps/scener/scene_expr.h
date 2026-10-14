#ifndef __SCENE_EXPR_H__
#define __SCENE_EXPR_H__

/* Static scene expressions: <let name value> and {expr} inside any attribute.
   A let is visible to the siblings that follow it and to their descendants. Values are
   evaluated once at load with the reel expression compiler; saving keeps the source text. */

#define SCENE_EXPR_NUMBER_FORMAT "%.7g"
#define SCENE_EXPR_NUMBER_CAPACITY 32

typedef struct { reel_t *reel; char (*names)[REEL_NAME]; float *values; int n,cap; } scene_let_scope_t;

static int scene_expr_eval(scene_let_scope_t *scope,const char *source,float *out,char *error,size_t errorSize){
	reel_t *r=scope->reel;
	r->let_names=scope->names; r->lets=scope->values; r->nlets=scope->n; r->error[0]=0;
	reel_expr_t e; memset(&e,0,sizeof(e));
	if(!reel_expr_compile(r,source,&e)){ snprintf(error,errorSize,"%s",r->error); return 0; }
	*out=reel_expr_eval(&e,r);
	reel_expr_free(&e);
	if(!isfinite(*out)){ snprintf(error,errorSize,"\"%s\" is not a finite number",source); return 0; }
	return 1;
}

static int scene_let_name_valid(const char *name){
	if(!name || !(isalpha((unsigned char)name[0]) || name[0]=='_') || strlen(name)>=REEL_NAME) return 0;
	for(const char *c=name;*c;c++) if(!isalnum((unsigned char)*c) && *c!='_') return 0;
	return 1;
}

static void scene_resolve_attr(Scene *s,scene_let_scope_t *scope,XmlNode *n,XmlAttr *a,const char *path){
	free(a->resolved); a->resolved=NULL;
	if(!strchr(a->value,'{')) return;
	size_t cap=strlen(a->value)+1,len=0; char *out=malloc(cap);
	for(const char *p=a->value;*p;){
		if(*p!='{'){ out[len++]=*p++; continue; }
		const char *end=strchr(p,'}');
		if(!end){
			scene_issue(s,SCENE_ISSUE_ERROR,"expression",path,n->line,"<%s %s=\"%s\">: unterminated '{'",n->tag,a->name,a->value);
			free(out); return;
		}
		char source[XML_VALUE_CAPACITY]; size_t sn=(size_t)(end-p-1);
		if(sn>=sizeof(source)) sn=sizeof(source)-1;
		memcpy(source,p+1,sn); source[sn]=0;
		float value; char error[512];
		if(!scene_expr_eval(scope,source,&value,error,sizeof(error))){
			scene_issue(s,SCENE_ISSUE_ERROR,"expression",path,n->line,"<%s %s>: %s",n->tag,a->name,error);
			free(out); return;
		}
		char number[SCENE_EXPR_NUMBER_CAPACITY];
		int nn=snprintf(number,sizeof(number),SCENE_EXPR_NUMBER_FORMAT,value==0?0.0f:value);
		cap+=(size_t)nn; out=realloc(out,cap);
		memcpy(out+len,number,(size_t)nn); len+=(size_t)nn;
		p=end+1;
	}
	out[len]=0;
	a->resolved=out;
}

static void scene_resolve_node(Scene *s,scene_let_scope_t *scope,XmlNode *n,const char *path){
	for(int i=0;i<n->nattrs;i++) scene_resolve_attr(s,scope,n,&n->attrs[i],path);
	int mark=scope->n;
	for(int i=0;i<n->nkids;i++){
		XmlNode *kid=n->kids[i];
		if(strcmp(kid->tag,"let")){ scene_resolve_node(s,scope,kid,path); continue; }
		const char *name=xml_attr(kid,"name",NULL),*source=xml_attr(kid,"value",NULL);
		float value; char error[512];
		if(!scene_let_name_valid(name)){ scene_issue(s,SCENE_ISSUE_ERROR,"expression",path,kid->line,"<let name=\"%s\">: a let needs an identifier name",name?name:""); continue; }
		if(!source){ scene_issue(s,SCENE_ISSUE_ERROR,"expression",path,kid->line,"<let name=\"%s\">: missing value",name); continue; }
		if(!scene_expr_eval(scope,source,&value,error,sizeof(error))){
			scene_issue(s,SCENE_ISSUE_ERROR,"expression",path,kid->line,"<let name=\"%s\">: %s",name,error); continue;
		}
		if(scope->n>=scope->cap){
			scope->cap=scope->cap?scope->cap*2:16;
			scope->names=realloc(scope->names,sizeof(*scope->names)*(size_t)scope->cap);
			scope->values=realloc(scope->values,sizeof(*scope->values)*(size_t)scope->cap);
		}
		snprintf(scope->names[scope->n],REEL_NAME,"%s",name);
		scope->values[scope->n++]=value;
	}
	scope->n=mark;
}

static void scene_resolve_expressions(Scene *s,XmlNode *root,const char *path){
	scene_let_scope_t scope; memset(&scope,0,sizeof(scope));
	scope.reel=calloc(1,sizeof(reel_t));
	scope.reel->scene_scope=true;
	scene_resolve_node(s,&scope,root,path);
	free(scope.reel); free(scope.names); free(scope.values);
}

#endif
