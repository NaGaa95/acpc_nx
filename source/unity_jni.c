/* Pocket Camp Android framework handlers. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <switch.h>

#include "unity_jni.h"
#include "config.h"
#include "editbox.h"

struct FakeID { uint32_t tag; char cls[96]; char name[64]; char sig[160]; };

#define REFRESH_HZ        60

static char g_root[192];
static char g_assets[208];

static int  has(const char *s, const char *sub){ return strstr(s,sub)!=NULL; }
static int  ret_is(const char *sig,const char *t){ const char*p=strchr(sig,')'); return p && strstr(p+1,t)==p+1; }

enum { UJ_TAG = 0x554a4831 /*'UJH1'*/ };
enum { UJ_INPUTSTREAM, UJ_AFD, UJ_FD, UJ_PREFS, UJ_EDITOR, UJ_GENERIC,
       UJ_MAP, UJ_SET, UJ_ITER, UJ_ENTRY, UJ_BOXED };

typedef struct {
  uint32_t tag; int kind;
  FILE *fp;
  int fd;
  long off, len;
  int idx;
  char btype;
  long long bival;
  double bfval;
} UHandle;

static UHandle *uh_new(int kind){
  UHandle *h = calloc(1,sizeof *h);
  h->tag = UJ_TAG; h->kind = kind; h->fd = -1; return h;
}
static int is_uh(void *p,int kind){ UHandle*h=p; return h && h->tag==UJ_TAG && h->kind==kind; }

static void asset_path(char *out,size_t n,const char *name){
  while (name && (name[0]=='/' )) name++;
  snprintf(out,n,"%s/%s",g_assets,name?name:"");
}

static const char *managed_root(void){
  const char *c = strchr(g_root, ':');
  return (c && c[1] == '/') ? c + 1 : g_root;
}

/* PlayerPrefs storage. */
typedef struct { char type; char *key; char *val; } KV;
static KV   *g_kv = NULL; static int g_kv_n=0, g_kv_cap=0; static int g_kv_dirty=0;

static char *prefs_file(char *buf,size_t n){ snprintf(buf,n,"%s/prefs.kv",g_root); return buf; }

static void kv_set(char type,const char*key,const char*val){
  for (int i=0;i<g_kv_n;i++) if(!strcmp(g_kv[i].key,key)){
    g_kv[i].type=type; free(g_kv[i].val); g_kv[i].val=strdup(val); g_kv_dirty=1; return; }
  if (g_kv_n==g_kv_cap){ g_kv_cap=g_kv_cap?g_kv_cap*2:32; g_kv=realloc(g_kv,g_kv_cap*sizeof(KV)); }
  g_kv[g_kv_n].type=type; g_kv[g_kv_n].key=strdup(key); g_kv[g_kv_n].val=strdup(val);
  g_kv_n++; g_kv_dirty=1;
}
static KV *kv_get(const char*key){ for(int i=0;i<g_kv_n;i++) if(!strcmp(g_kv[i].key,key)) return &g_kv[i]; return NULL; }
static void kv_remove(const char*key){
  for(int i=0;i<g_kv_n;i++) if(!strcmp(g_kv[i].key,key)){
    free(g_kv[i].key);free(g_kv[i].val); g_kv[i]=g_kv[--g_kv_n]; g_kv_dirty=1; return; }
}
static void kv_clear(void){ for(int i=0;i<g_kv_n;i++){free(g_kv[i].key);free(g_kv[i].val);} g_kv_n=0; g_kv_dirty=1; }

static void esc(FILE*f,const char*s){ for(;*s;s++){ if(*s=='\\'||*s=='\t'||*s=='\n'){fputc('\\',f);
  fputc(*s=='\t'?'t':*s=='\n'?'n':'\\',f);} else fputc(*s,f);} }
static char *unesc(char*s){ char*o=s,*w=s; for(;*o;o++){ if(*o=='\\'&&o[1]){o++;
  *w++=(*o=='t')?'\t':(*o=='n')?'\n':*o;} else *w++=*o;} *w=0; return s; }

static void prefs_load(void){
  char p[256]; FILE*f=fopen(prefs_file(p,sizeof p),"rb");
  if(!f) return;
  char line[2048];
  while (fgets(line,sizeof line,f)){
    char *nl=strchr(line,'\n'); if(nl)*nl=0;
    if(!line[0]) continue;
    char type=line[0]; char *k=line+2;            /* "T\tkey\tval"          */
    char *t1=strchr(k,'\t'); if(!t1) continue; *t1=0; char*v=t1+1;
    kv_set(type, unesc(k), unesc(v));
  }
  fclose(f); g_kv_dirty=0;
}
static void prefs_flush(void){
  if(!g_kv_dirty) return;
  char p[256]; FILE*f=fopen(prefs_file(p,sizeof p),"wb");
  if(!f) return;
  for(int i=0;i<g_kv_n;i++){ fputc(g_kv[i].type,f); fputc('\t',f);
    esc(f,g_kv[i].key); fputc('\t',f); esc(f,g_kv[i].val); fputc('\n',f); }
  fclose(f); g_kv_dirty=0;
}

/* Box primitive PlayerPrefs values returned by getAll(). */
static void *uh_box_from_kv(const KV *kv){
  if (!kv) return jni_make_string("");
  switch (kv->type){
    case 'I': case 'L': case 'B': {
      UHandle *h = uh_new(UJ_BOXED);
      h->btype = kv->type;
      h->bival = strtoll(kv->val, NULL, 10);
      if (kv->type=='B') h->bival = (kv->val[0]=='1'||kv->val[0]=='t'||kv->val[0]=='T') ? 1 : 0;
      return h;
    }
    case 'F': {
      UHandle *h = uh_new(UJ_BOXED);
      h->btype = 'F'; h->bfval = strtod(kv->val, NULL);
      return h;
    }
    default: /* 'S' and anything else -> string */
      return jni_make_string(kv->val);
  }
}

int unity_is_boxed(void *p){
  UHandle *h = p; return (h && h->tag==UJ_TAG && h->kind==UJ_BOXED) ? 1 : 0;
}
uint64_t unity_boxed_int(void *p){
  UHandle *h = p; if (!unity_is_boxed(h)) return 0;
  if (h->btype=='F') return (uint64_t)(long long)h->bfval;
  return (uint64_t)h->bival;
}
float unity_boxed_float(void *p){
  UHandle *h = p; if (!unity_is_boxed(h)) return 0.0f;
  return (h->btype=='F') ? (float)h->bfval : (float)h->bival;
}
int unity_isinstance(void *p, const char *clazz){
  UHandle *h = p; if (!h || h->tag!=UJ_TAG || h->kind!=UJ_BOXED) return -1;
  if (!clazz) return 0;
  switch (h->btype){
    case 'I': return strstr(clazz,"Integer") ? 1 : 0;
    case 'L': return strstr(clazz,"Long")    ? 1 : 0;
    case 'F': return strstr(clazz,"Float")   ? 1 : 0;
    case 'B': return strstr(clazz,"Boolean") ? 1 : 0;
  }
  return 0;
}

int unity_owns_class(const char *cls){
  return has(cls,"AssetManager") || has(cls,"java/io/InputStream") ||
         has(cls,"AssetFileDescriptor") || has(cls,"java/io/FileDescriptor") ||
         has(cls,"SharedPreferences") || has(cls,"SharedPreferences$Editor") ||
         has(cls,"java/util/Map") || has(cls,"java/util/Set") ||
         has(cls,"java/util/Iterator") || has(cls,"java/util/HashMap") ||
         has(cls,"view/Display") || has(cls,"DisplayManager") ||
         has(cls,"res/Configuration") || has(cls,"res/Resources") ||
         has(cls,"DisplayMetrics") || has(cls,"content/Context") ||
         has(cls,"unity3d/player/UnityPlayer");
}

void *unity_dispatch_object(void *recv, const void *id_, va_list va){ const struct FakeID *id = id_;
  const char *cls=id->cls, *m=id->name;

  /* AssetManager streams and descriptors. */
  if (has(cls,"AssetManager")){
    if (has(m,"openFd") || has(m,"openNonAssetFd")){
      const char *name = jni_string_utf(va_arg(va,void*));
      char path[320]; asset_path(path,sizeof path,name);
      int fd = open(path,O_RDONLY);
      if(fd<0) return NULL;
      struct stat st; fstat(fd,&st);
      UHandle*h=uh_new(UJ_AFD); h->fd=fd; h->off=0; h->len=st.st_size; return h;
    }
    if (has(m,"open")){
      const char *name = jni_string_utf(va_arg(va,void*));
      char path[320]; asset_path(path,sizeof path,name);
      FILE*fp=fopen(path,"rb");
      if(!fp) return NULL;
      UHandle*h=uh_new(UJ_INPUTSTREAM); h->fp=fp; return h;
    }
    if (has(m,"list")) return jni_make_object("String[]");
    return jni_make_object("AssetManager");
  }

  if (has(cls,"AssetFileDescriptor")){
    if (has(m,"getFileDescriptor") || has(m,"getParcelFileDescriptor")){
      UHandle*a=recv; UHandle*fd=uh_new(UJ_FD); fd->fd = is_uh(a,UJ_AFD)?a->fd:-1; return fd;
    }
    return jni_make_object("AssetFileDescriptor");
  }

  /* SharedPreferences objects. */
  if (has(cls,"SharedPreferences") && !has(cls,"Editor")){
    if (has(m,"edit")) return uh_new(UJ_EDITOR);
    if (has(m,"getString")){
      const char *key = jni_string_utf(va_arg(va,void*));
      KV*kv=kv_get(key);
      return jni_make_string(kv?kv->val: (ret_is(id->sig,"Ljava/lang/String;")? "" : "") );
    }
    if (has(m,"getAll")){                 /* Unity PlayerPrefs LOAD entry point */
      return uh_new(UJ_MAP);
    }
    if (has(m,"getStringSet")) return jni_make_object("Set");
    return jni_make_object("SharedPreferences");
  }

  if (has(cls,"java/util/Map") && !has(cls,"Entry")){
    if (has(m,"entrySet") || has(m,"keySet")) return uh_new(UJ_SET);
    if (has(m,"get")){ const char*k=jni_string_utf(va_arg(va,void*)); return uh_box_from_kv(kv_get(k)); }
    return uh_new(UJ_MAP);
  }
  if (has(cls,"java/util/Set")){
    if (has(m,"iterator")){ UHandle*it=uh_new(UJ_ITER); it->idx=0; return it; }
    return uh_new(UJ_SET);
  }
  if (has(cls,"java/util/Iterator")){
    if (has(m,"next")){                   /* return current entry, advance cursor */
      UHandle*it=recv;
      int i = is_uh(it,UJ_ITER) ? it->idx : 0;
      if (is_uh(it,UJ_ITER)) it->idx++;
      UHandle*e=uh_new(UJ_ENTRY); e->idx=i; return e;
    }
    return jni_make_object("java/util/Iterator");
  }
  if (has(cls,"java/util/Map") && has(cls,"Entry")){   /* java/util/Map$Entry */
    UHandle*e=recv; int i = is_uh(e,UJ_ENTRY) ? e->idx : -1;
    if (i<0 || i>=g_kv_n) return jni_make_string("");
    if (has(m,"getKey"))   return jni_make_string(g_kv[i].key);
    if (has(m,"getValue")) return uh_box_from_kv(&g_kv[i]);
    return jni_make_string("");
  }
  if (has(cls,"SharedPreferences$Editor")){
    if (has(m,"putString")){ const char*k=jni_string_utf(va_arg(va,void*));
      const char*v=jni_string_utf(va_arg(va,void*));
      if(!k[0]) return recv;
      kv_set('S',k,v); return recv; }
    if (has(m,"putInt")){ const char*k=jni_string_utf(va_arg(va,void*));
      int v=va_arg(va,int); char b[32]; snprintf(b,sizeof b,"%d",v);
      if(!k[0]) return recv;
      kv_set('I',k,b); return recv; }
    if (has(m,"putLong")){ const char*k=jni_string_utf(va_arg(va,void*));
      long long v=va_arg(va,long long); char b[32]; snprintf(b,sizeof b,"%lld",v);
      if(!k[0]) return recv;
      kv_set('L',k,b); return recv; }
    if (has(m,"putFloat")){ const char*k=jni_string_utf(va_arg(va,void*));
      double v=va_arg(va,double); char b[32]; snprintf(b,sizeof b,"%.9g",v);
      if(!k[0]) return recv;
      kv_set('F',k,b); return recv; }
    if (has(m,"putBoolean")){ const char*k=jni_string_utf(va_arg(va,void*));
      int v=va_arg(va,int);
      if(!k[0]) return recv;
      kv_set('B',k,v?"1":"0"); return recv; }
    if (has(m,"remove")){ const char*k=jni_string_utf(va_arg(va,void*)); kv_remove(k); return recv; }
    if (has(m,"clear")){ kv_clear(); return recv; }
    return recv;
  }

  if (has(cls,"DisplayManager") && has(m,"getDisplay")) return jni_make_object("Display");
  if (has(cls,"res/Resources")){
    if (has(m,"getConfiguration")) return jni_make_object("Configuration");
    if (has(m,"getDisplayMetrics")) return jni_make_object("DisplayMetrics");
    return jni_make_object("Resources");
  }
  if (has(cls,"content/Context")){
    if (has(m,"getFilesDir")||has(m,"getCacheDir")||has(m,"getDataDir")||has(m,"getExternalFilesDir"))
      return jni_make_object("File");                 /* File.getAbsolutePath -> g_root below */
    if (has(m,"getPackageName")) return jni_make_string(ACPC_PACKAGE);
    if (has(m,"getPackageCodePath")||has(m,"getPackageResourcePath")) return jni_make_string(managed_root());
    if (has(m,"getAssets")) return jni_make_object("AssetManager");
    if (has(m,"getResources")) return jni_make_object("Resources");
    if (has(m,"getSystemService")) return jni_make_object("Service");
    return jni_make_object("Context");
  }
  if (has(cls,"java/io/File") && (has(m,"getAbsolutePath")||has(m,"getPath")||has(m,"toString")))
    return jni_make_string(managed_root());

  if (has(cls,"UnityPlayer")) return jni_make_object("UnityPlayer");

  return jni_make_object(cls); /* default: opaque handle, never NULL */
}

uint64_t unity_dispatch_int(void *recv, const void *id_, va_list va){ const struct FakeID *id = id_;
  const char *cls=id->cls, *m=id->name;

  /* SharedPreferences iteration and unboxing. */
  if (has(cls,"java/util/Iterator") && has(m,"hasNext")){
    UHandle*it=recv; return (uint64_t)((is_uh(it,UJ_ITER) && it->idx < g_kv_n) ? 1 : 0);
  }
  if (has(cls,"java/util/Map") && !has(cls,"Entry")){
    if (has(m,"size"))    return (uint64_t)g_kv_n;
    if (has(m,"isEmpty")) return (uint64_t)(g_kv_n==0);
    if (has(m,"containsKey")){ const char*k=jni_string_utf(va_arg(va,void*)); return (uint64_t)(kv_get(k)?1:0); }
  }

  /* InputStream.read() / read([B) / read([B,off,len) / available / skip */
  if (has(cls,"java/io/InputStream")){
    UHandle*h=recv; if(!is_uh(h,UJ_INPUTSTREAM)||!h->fp) return (uint64_t)-1;
    if (has(m,"available")){ long cur=ftell(h->fp); fseek(h->fp,0,SEEK_END);
      long end=ftell(h->fp); fseek(h->fp,cur,SEEK_SET); return (uint64_t)(end-cur); }
    if (has(m,"skip")){ long nskip=(long)va_arg(va,long long); fseek(h->fp,nskip,SEEK_CUR); return (uint64_t)nskip; }
    if (has(m,"close")){ fclose(h->fp); h->fp=NULL; return 0; }
    if (has(m,"read")){
      if (strstr(id->sig,"([B")){                     /* read(byte[][,off,len]) */
        void *arr = va_arg(va,void*);
        int alen=0; char *buf = jni_bytearray_data(arr,&alen);
        int off=0, len=alen;
        if (strstr(id->sig,"([BII)")){ off=va_arg(va,int); len=va_arg(va,int); }
        size_t got=fread(buf+off,1,(size_t)len,h->fp);
        return got? (uint64_t)got : (uint64_t)-1;     /* -1 == EOF, per InputStream */
      }
      int c=fgetc(h->fp); return (uint64_t)(c==EOF? -1 : c); /* read() one byte */
    }
    return 0;
  }

  /* SharedPreferences getters (Int/Long/Boolean + contains) */
  if (has(cls,"SharedPreferences") && !has(cls,"Editor")){
    if (has(m,"contains")){ const char*k=jni_string_utf(va_arg(va,void*)); return kv_get(k)?1:0; }
    if (has(m,"getInt")||has(m,"getLong")){ const char*k=jni_string_utf(va_arg(va,void*));
      long long def=(long long)va_arg(va,long long); KV*kv=kv_get(k);
      return (uint64_t)(kv? strtoll(kv->val,NULL,10) : def); }
    if (has(m,"getBoolean")){ const char*k=jni_string_utf(va_arg(va,void*));
      int def=va_arg(va,int); KV*kv=kv_get(k); return (uint64_t)(kv? (kv->val[0]=='1'||kv->val[0]=='t') : def); }
    return 0;
  }
  if (has(cls,"SharedPreferences$Editor")){
    if (has(m,"commit")){ prefs_flush(); return 1; }
    if (has(m,"putInt")||has(m,"putLong")){ const char*k=jni_string_utf(va_arg(va,void*));
      long long v=(long long)va_arg(va,long long); char b[32]; snprintf(b,sizeof b,"%lld",v);
      kv_set(has(m,"putLong")?'L':'I',k,b); return (uint64_t)(uintptr_t)recv; }
    if (has(m,"putBoolean")){ const char*k=jni_string_utf(va_arg(va,void*));
      int v=va_arg(va,int); kv_set('B',k,v?"1":"0"); return (uint64_t)(uintptr_t)recv; }
    return (uint64_t)(uintptr_t)recv;
  }

  /* AssetFileDescriptor.getStartOffset()/getLength()/getDeclaredLength() (long) */
  if (has(cls,"AssetFileDescriptor")){
    UHandle*a=recv;
    if (has(m,"getStartOffset")) return (uint64_t)(is_uh(a,UJ_AFD)?a->off:0);
    if (has(m,"getLength")||has(m,"getDeclaredLength")) return (uint64_t)(is_uh(a,UJ_AFD)?a->len:0);
    return 0;
  }
  if (has(cls,"java/io/FileDescriptor")){ UHandle*f=recv; return (uint64_t)(is_uh(f,UJ_FD)?(unsigned)f->fd:0); }

  if (has(cls,"view/Display")||has(cls,"DisplayMetrics")||has(cls,"DisplayManager")){
    if (has(m,"getWidth")||has(m,"WidthPixels")||has(m,"getRawWidth"))  return screen_width;
    if (has(m,"getHeight")||has(m,"HeightPixels")||has(m,"getRawHeight"))return screen_height;
    if (has(m,"getRotation")) return 0;
    if (has(m,"getDisplayId")) return 0;
    return 0;
  }
  return 0;
}

void unity_dispatch_void(void *recv, const void *id_, va_list va){ const struct FakeID *id = id_;
  const char *cls=id->cls, *m=id->name;
  if (has(cls,"java/io/InputStream") && has(m,"close")){ UHandle*h=recv;
    if(is_uh(h,UJ_INPUTSTREAM)&&h->fp){fclose(h->fp);h->fp=NULL;} return; }
  if (has(cls,"AssetFileDescriptor") && has(m,"close")){ UHandle*a=recv;
    if(is_uh(a,UJ_AFD)&&a->fd>=0){close(a->fd);a->fd=-1;} return; }
  if (has(cls,"SharedPreferences$Editor") && has(m,"apply")){ prefs_flush(); return; }
  if (has(cls,"SharedPreferences$Editor") && has(m,"putString")){ /* if routed here as void */
    const char*k=jni_string_utf(va_arg(va,void*)); const char*v=jni_string_utf(va_arg(va,void*));
    kv_set('S',k,v); return; }
  if (has(cls,"UnityPlayer")){
    if (!strcmp(m,"showSoftInput")) {
      const char *initial = jni_string_utf(va_arg(va,void*));
      (void)va_arg(va,int); /* keyboard type */
      (void)va_arg(va,int); /* autocorrection */
      (void)va_arg(va,int); /* multiline */
      (void)va_arg(va,int); /* secure */
      (void)va_arg(va,int); /* alert */
      (void)va_arg(va,void*); /* placeholder */
      int limit = va_arg(va,int);
      editbox_show(initial, limit);
      return;
    }
    if (!strcmp(m,"hideSoftInput")) { editbox_close(); return; }
    return;
  }
  (void)recv;(void)va;
}

void unity_jni_init(const char *data_root){
  snprintf(g_root,sizeof g_root,"%s",data_root && *data_root ? data_root : GAME_HOME);
  snprintf(g_assets,sizeof g_assets,"%s/assets",g_root);
  prefs_load();
}
