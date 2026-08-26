#include "fst.h"
#include "fst_utils.h"
#include <stdio.h>

#include <string>
#include <string.h>

#include <map>

static std::map<AEffect*, AEffectDispatcherProc>s_host2plugin;
static std::map<AEffect*, AEffectDispatcherProc>s_plugin2host;
static std::map<AEffect*, std::string>s_pluginname;
static AEffectDispatcherProc s_plug2host;

static
t_fstPtrInt host2plugin (AEffect* effect, int opcode, int index, t_fstPtrInt ivalue, void*ptr, float fvalue) {
  //printf("%s(%d, %d, %lld, %p, %f)\n",  __FUNCTION__, opcode, index, ivalue, ptr, fvalue); fflush(stdout);
  switch(opcode) {
  case effGetVendorString:
    printf("getVendorString\n");
    snprintf((char*)ptr, 16, "ProxyVendor");
    return 1;
  case effGetEffectName:
    printf("getEffectName\n");
    snprintf((char*)ptr, 16, "ProxyEffect");
    return 1;
  case 26:
    printf("OPCODE26: %d\n", index);
    return (index<5);
  case 42:
    printf("OPCODE42: %d, %lld, %p, %f\n", index, ivalue, ptr, fvalue);fflush(stdout);
    break;
  case effVendorSpecific:
    printf("effVendorSpecific(0x%X, 0x%X)\n", index, ivalue);
    print_hex(ptr, 256);
    break;
  case 56:
#if 0
    printf("OPCODE56\n");
    print_hex(ptr, 256);
    return dispatch_effect("???", s_host2plugin[effect], effect, opcode, index, ivalue, 0, fvalue);
#endif
    break;
  case 62:
    return 0;
    printf("OPCODE62?\n");
    print_hex(ptr, 256);
      // >=90: stack smashing
      // <=85: ok
    snprintf((char*)ptr, 85, "JMZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ");
  case 66: {
#if 0
    char*cptr = (char*)ptr;
    int*iptr = (int*)ptr;
    printf("OPCODE66: %3d %3d\n", iptr[0], iptr[1]);
    if (60 == iptr[1])
      snprintf((char*)ptr+8, 52, "middle C", iptr[1]);
    else
      snprintf((char*)ptr+8, 52, "note:#%d", iptr[1]);
    #endif
  }
    //return 1;
  default:
    break;
  }
  AEffectDispatcherProc h2p = s_host2plugin[effect];
  if(!h2p) {
    printf("Fst::host2plugin:: NO CALLBACK!\n");
    return 0xDEAD;
  }
  const char*pluginname = 0;
  if(effect)
    pluginname = s_pluginname[effect].c_str();

  bool doPrint = true;
#ifdef FST_EFFKNOWN
  doPrint = !effKnown(opcode);
#endif
  switch(opcode) {
  default: break;
  case 56: case 53:
  case effGetChunk: case effSetChunk:
  case effVendorSpecific:
    doPrint = false;
    break;
  }
  t_fstPtrInt result = 0;
  if(doPrint) {
    dispatch_effect(pluginname, h2p, effect, opcode, index, ivalue, ptr, fvalue);
  } else {
    result = h2p(effect, opcode, index, ivalue, ptr, fvalue);
  }
  switch(opcode) {
  default: break;
  case 56:
    print_hex(ptr, 256);
    break;
  case 62:
    printf("OPCODE62!\n");
    print_hex(ptr, 256);
  }
  return result;
}
static
t_fstPtrInt host2plugin_wrapper (AEffect* effect, int opcode, int index, t_fstPtrInt ivalue, void*ptr, float fvalue)
{
  t_fstPtrInt result;
  fflush(stdout);
  result = host2plugin(effect, opcode, index, ivalue, ptr, fvalue);
  fflush(stdout);

  return result;
}
static
t_fstPtrInt plugin2host (AEffect* effect, int opcode, int index, t_fstPtrInt ivalue, void*ptr, float fvalue) {
  //printf("%s:%d\n", __FUNCTION__, opcode); fflush(stdout);
  AEffectDispatcherProc p2h = s_plugin2host[effect];
  if(!p2h)p2h = s_plug2host;
  if(effect && !s_host2plugin[effect]) {
    s_host2plugin[effect] = effect->dispatcher;
    effect->dispatcher = host2plugin_wrapper;
  }
  const char*pluginname = 0;
  if(effect)
    pluginname = s_pluginname[effect].c_str();

  bool doPrint = true;
#ifdef FST_HOSTKNOWN
  doPrint = !hostKnown(opcode);
#endif
  switch(opcode) {
  default: break;
  case 14:
    printf("hostCODE14\n");
    //return 1;
    break;
  case 42:
    printf("hostCODE42\n");
    //return 0;
    break;
  case audioMasterGetCurrentProcessLevel:
  case audioMasterGetTime:
    doPrint = false;
    break;
  case (int)0xDEADBEEF:
    printf("0xDEADBEEF\n");
  }
  t_fstPtrInt result = -1;

  fflush(stdout);
  if(doPrint) {
    if(0xDEADBEEF ==opcode) {
      unsigned int uindex = (unsigned int) index;
      switch(uindex) {
      case 0xDEADF00D:
        printf("\t0x%X/0x%X '%s' ->\n", opcode, index, ptr);
        break;
      default:
        printf("\t0x%X/0x%X ->\n", opcode, index);
        break;
      }
    }

    result = dispatch_host(pluginname, p2h, effect, opcode, index, ivalue, ptr, fvalue);
  } else {
    result = p2h(effect, opcode, index, ivalue, ptr, fvalue);
  }
  fflush(stdout);
  return result;
}


extern "C"
AEffect*VSTPluginMain(AEffectDispatcherProc dispatch4host) {
  char pluginname[512] = {0};
  char*pluginfile = getenv("FST_PROXYPLUGIN");
  printf("FstProxy: %s\n", pluginfile);
  if(!pluginfile)return 0;
  s_plug2host = dispatch4host;

  t_fstMain*plugMain = fstLoadPlugin(pluginfile);
  if(!plugMain)return 0;

  AEffect*plug = plugMain(plugin2host);
  if(!plug)
    return plug;

  printf("plugin.dispatcher '%p' -> '%p'\n", plug->dispatcher, host2plugin_wrapper);
  if(plug->dispatcher != host2plugin_wrapper) {
    s_host2plugin[plug] = plug->dispatcher;
    plug->dispatcher = host2plugin_wrapper;
  }

  s_host2plugin[plug](plug, effGetEffectName, 0, 0, pluginname, 0);
  if(*pluginname)
    s_pluginname[plug] = pluginname;
  else
    s_pluginname[plug] = pluginfile;

  s_plugin2host[plug] = dispatch4host;
  print_aeffect(plug);
  fflush(stdout);
  return plug;
}
